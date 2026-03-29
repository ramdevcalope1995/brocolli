/*
 * brocolli - workflow.c
 *
 * SQLite schema (created automatically on workflow_init):
 *
 *   jobs
 *   ├── id          TEXT  PRIMARY KEY  (UUID v4)
 *   ├── type        TEXT  NOT NULL     (job type string)
 *   ├── payload     TEXT  NOT NULL     (arbitrary JSON)
 *   ├── status      TEXT  NOT NULL     ('pending','running','done','failed')
 *   ├── retry_count INTEGER DEFAULT 0
 *   ├── run_at      INTEGER NOT NULL   (unix epoch: earliest pickup time)
 *   └── created_at  INTEGER NOT NULL   (unix epoch: insertion time)
 *
 * Worker loop (runs in a background pthread):
 *
 *   every WORKFLOW_POLL_MS milliseconds:
 *     SELECT * FROM jobs
 *     WHERE status='pending' AND run_at <= now()
 *     ORDER BY run_at ASC LIMIT 1
 *
 *   → call registered handler(job_id, type, payload)
 *   → handler returns 0  → UPDATE status='done'
 *   → handler returns !0 → retry_count++
 *                           if retry_count >= MAX_RETRIES → status='failed'
 *                           else → run_at = now + BACKOFF_BASE^retry_count
 *                                  status = 'pending'
 */

#include "workflow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

/* SQLite3 amalgamation — must be in vendor/sqlite3/ */
#include "../vendor/sqlite3/sqlite3.h"

/* ------------------------------------------------------------------
 * UUID helper (identical to sandbox.c to avoid extra dependency)
 * ------------------------------------------------------------------ */

#include <fcntl.h>

static void wf_gen_uuid(char buf[WORKFLOW_JOB_ID_LEN])
{
    unsigned char b[16];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0 || read(fd, b, 16) != 16) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        unsigned long v = (unsigned long)ts.tv_nsec ^ ((unsigned long)getpid() << 32);
        memset(b, 0, 16);
        memcpy(b, &v, sizeof(v));
    }
    if (fd >= 0) close(fd);
    b[6] = (b[6] & 0x0f) | 0x40;
    b[8] = (b[8] & 0x3f) | 0x80;
    snprintf(buf, WORKFLOW_JOB_ID_LEN,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],
        b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15]);
}

/* ------------------------------------------------------------------
 * Handler registry
 * ------------------------------------------------------------------ */

#define MAX_HANDLERS 64

typedef struct {
    char              type[WORKFLOW_TYPE_MAX];
    workflow_handler_t fn;
} handler_entry_t;

static handler_entry_t s_handlers[MAX_HANDLERS];
static int             s_handler_count = 0;
static pthread_mutex_t s_handler_mutex = PTHREAD_MUTEX_INITIALIZER;

int workflow_register(const char *type, workflow_handler_t handler)
{
    pthread_mutex_lock(&s_handler_mutex);
    if (s_handler_count >= MAX_HANDLERS) {
        pthread_mutex_unlock(&s_handler_mutex);
        return -1;
    }
    strncpy(s_handlers[s_handler_count].type, type, WORKFLOW_TYPE_MAX - 1);
    s_handlers[s_handler_count].fn = handler;
    s_handler_count++;
    pthread_mutex_unlock(&s_handler_mutex);
    return 0;
}

static workflow_handler_t find_handler(const char *type)
{
    pthread_mutex_lock(&s_handler_mutex);
    workflow_handler_t fn = NULL;
    for (int i = 0; i < s_handler_count; i++) {
        if (strncmp(s_handlers[i].type, type, WORKFLOW_TYPE_MAX) == 0) {
            fn = s_handlers[i].fn;
            break;
        }
    }
    pthread_mutex_unlock(&s_handler_mutex);
    return fn;
}

/* ------------------------------------------------------------------
 * SQLite helpers
 * ------------------------------------------------------------------ */

static sqlite3       *s_db    = NULL;
static pthread_mutex_t s_db_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Execute a single SQL statement with no result rows. */
static int db_exec(const char *sql)
{
    char *errmsg = NULL;
    int rc = sqlite3_exec(s_db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[workflow] db_exec error: %s\n", errmsg ? errmsg : "?");
        sqlite3_free(errmsg);
        return -1;
    }
    return 0;
}

static int db_open(const char *path)
{
    int rc = sqlite3_open(path, &s_db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[workflow] sqlite3_open(%s): %s\n", path, sqlite3_errmsg(s_db));
        return -1;
    }

    /* WAL mode: allows readers and the worker writer to coexist */
    db_exec("PRAGMA journal_mode=WAL;");
    db_exec("PRAGMA synchronous=NORMAL;");
    db_exec("PRAGMA foreign_keys=ON;");

    /* Create table */
    const char *create_sql =
        "CREATE TABLE IF NOT EXISTS jobs ("
        "  id          TEXT    PRIMARY KEY,"
        "  type        TEXT    NOT NULL,"
        "  payload     TEXT    NOT NULL,"
        "  status      TEXT    NOT NULL DEFAULT 'pending',"
        "  retry_count INTEGER NOT NULL DEFAULT 0,"
        "  run_at      INTEGER NOT NULL,"
        "  created_at  INTEGER NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_jobs_pickup "
        "  ON jobs(status, run_at);";

    return db_exec(create_sql);
}

/* ------------------------------------------------------------------
 * Worker thread
 * ------------------------------------------------------------------ */

static pthread_t s_worker;
static volatile int s_stop = 0;

/*
 * worker_pick_one() — claim one eligible job under a transaction.
 *
 * Returns 1 if a job was found and processed, 0 if nothing was ready.
 *
 * We use an UPDATE...RETURNING pattern to atomically claim the job:
 *   1. Find the oldest pending job whose run_at <= now.
 *   2. Set status='running' in the same statement.
 *   3. Run the handler.
 *   4. Update to 'done' or schedule a retry.
 *
 * The mutex prevents two worker threads from claiming the same row
 * (only one worker thread exists here, but it costs nothing to guard).
 */
static int worker_pick_one(void)
{
    pthread_mutex_lock(&s_db_mutex);

    time_t now = time(NULL);

    /* Step 1: find the oldest ready job */
    sqlite3_stmt *sel_stmt = NULL;
    const char *sel_sql =
        "SELECT id, type, payload, retry_count FROM jobs "
        "WHERE status='pending' AND run_at <= ? "
        "ORDER BY run_at ASC LIMIT 1;";

    int rc = sqlite3_prepare_v2(s_db, sel_sql, -1, &sel_stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[workflow] prepare select: %s\n", sqlite3_errmsg(s_db));
        pthread_mutex_unlock(&s_db_mutex);
        return 0;
    }
    sqlite3_bind_int64(sel_stmt, 1, (sqlite3_int64)now);

    char job_id[WORKFLOW_JOB_ID_LEN]  = {0};
    char job_type[WORKFLOW_TYPE_MAX]   = {0};
    char *job_payload = NULL;
    int  retry_count  = 0;

    if (sqlite3_step(sel_stmt) == SQLITE_ROW) {
        strncpy(job_id,   (const char *)sqlite3_column_text(sel_stmt, 0), WORKFLOW_JOB_ID_LEN  - 1);
        strncpy(job_type, (const char *)sqlite3_column_text(sel_stmt, 1), WORKFLOW_TYPE_MAX    - 1);
        const char *p = (const char *)sqlite3_column_text(sel_stmt, 2);
        job_payload = p ? strdup(p) : strdup("{}");
        retry_count = sqlite3_column_int(sel_stmt, 3);
    }
    sqlite3_finalize(sel_stmt);

    if (!job_id[0]) {
        pthread_mutex_unlock(&s_db_mutex);
        return 0; /* nothing to do */
    }

    /* Step 2: mark as running */
    sqlite3_stmt *upd_stmt = NULL;
    sqlite3_prepare_v2(s_db,
        "UPDATE jobs SET status='running' WHERE id=? AND status='pending';",
        -1, &upd_stmt, NULL);
    sqlite3_bind_text(upd_stmt, 1, job_id, -1, SQLITE_STATIC);
    sqlite3_step(upd_stmt);
    int claimed = sqlite3_changes(s_db);
    sqlite3_finalize(upd_stmt);

    pthread_mutex_unlock(&s_db_mutex);

    if (!claimed) {
        /* another thread claimed it between our SELECT and UPDATE */
        free(job_payload);
        return 0;
    }

    fprintf(stderr, "[workflow] running job %s type=%s retry=%d\n",
            job_id, job_type, retry_count);

    /* Step 3: call the handler (outside the lock — may take a while) */
    workflow_handler_t fn = find_handler(job_type);
    int handler_rc = -1;

    if (fn) {
        handler_rc = fn(job_id, job_type, job_payload);
    } else {
        fprintf(stderr, "[workflow] no handler for type '%s'\n", job_type);
    }

    free(job_payload);

    /* Step 4: update outcome */
    pthread_mutex_lock(&s_db_mutex);

    if (handler_rc == 0) {
        /* success */
        sqlite3_stmt *done_stmt = NULL;
        sqlite3_prepare_v2(s_db,
            "UPDATE jobs SET status='done' WHERE id=?;",
            -1, &done_stmt, NULL);
        sqlite3_bind_text(done_stmt, 1, job_id, -1, SQLITE_STATIC);
        sqlite3_step(done_stmt);
        sqlite3_finalize(done_stmt);
        fprintf(stderr, "[workflow] job %s done\n", job_id);
    } else {
        /* failure: retry or give up */
        int new_retry = retry_count + 1;
        if (new_retry >= WORKFLOW_MAX_RETRIES) {
            sqlite3_stmt *fail_stmt = NULL;
            sqlite3_prepare_v2(s_db,
                "UPDATE jobs SET status='failed', retry_count=? WHERE id=?;",
                -1, &fail_stmt, NULL);
            sqlite3_bind_int(fail_stmt,  1, new_retry);
            sqlite3_bind_text(fail_stmt, 2, job_id, -1, SQLITE_STATIC);
            sqlite3_step(fail_stmt);
            sqlite3_finalize(fail_stmt);
            fprintf(stderr, "[workflow] job %s FAILED (exhausted retries)\n", job_id);
        } else {
            /* exponential backoff: 3^retry seconds */
            long backoff = 1;
            for (int i = 0; i < new_retry; i++) backoff *= WORKFLOW_BACKOFF_BASE;
            time_t next_run = time(NULL) + backoff;

            sqlite3_stmt *retry_stmt = NULL;
            sqlite3_prepare_v2(s_db,
                "UPDATE jobs SET status='pending', retry_count=?, run_at=? WHERE id=?;",
                -1, &retry_stmt, NULL);
            sqlite3_bind_int(retry_stmt,   1, new_retry);
            sqlite3_bind_int64(retry_stmt, 2, (sqlite3_int64)next_run);
            sqlite3_bind_text(retry_stmt,  3, job_id, -1, SQLITE_STATIC);
            sqlite3_step(retry_stmt);
            sqlite3_finalize(retry_stmt);
            fprintf(stderr, "[workflow] job %s retry %d in %lds\n",
                    job_id, new_retry, backoff);
        }
    }

    pthread_mutex_unlock(&s_db_mutex);
    return 1;
}

/*
 * worker_thread() — the background thread that drives the queue.
 */
static void *worker_thread(void *arg)
{
    (void)arg;
    fprintf(stderr, "[workflow] worker thread started\n");

    while (!s_stop) {
        int did_work = worker_pick_one();

        if (!did_work) {
            /*
             * Nothing ready: sleep for WORKFLOW_POLL_MS.
             * We use nanosleep in a loop so SIGINT can interrupt us
             * and s_stop gets checked promptly.
             */
            struct timespec ts = {
                .tv_sec  = WORKFLOW_POLL_MS / 1000,
                .tv_nsec = (WORKFLOW_POLL_MS % 1000) * 1000000L
            };
            nanosleep(&ts, NULL);
        }
        /* if we did work, immediately try to pick another job */
    }

    fprintf(stderr, "[workflow] worker thread exiting\n");
    return NULL;
}

/* ------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------ */

int workflow_init(const char *db_path)
{
    if (s_db) {
        fprintf(stderr, "[workflow] already initialised\n");
        return 0;
    }

    if (db_open(db_path) != 0) return -1;

    s_stop = 0;
    if (pthread_create(&s_worker, NULL, worker_thread, NULL) != 0) {
        perror("[workflow] pthread_create");
        sqlite3_close(s_db);
        s_db = NULL;
        return -1;
    }

    fprintf(stderr, "[workflow] init ok, db=%s\n", db_path);
    return 0;
}

void workflow_shutdown(void)
{
    s_stop = 1;
    pthread_join(s_worker, NULL);

    pthread_mutex_lock(&s_db_mutex);
    if (s_db) {
        sqlite3_close(s_db);
        s_db = NULL;
    }
    pthread_mutex_unlock(&s_db_mutex);

    fprintf(stderr, "[workflow] shutdown complete\n");
}

int workflow_enqueue(const char *type,
                     const char *payload,
                     int         delay_sec,
                     char        out_id[WORKFLOW_JOB_ID_LEN])
{
    if (!s_db) {
        fprintf(stderr, "[workflow] not initialised\n");
        return -1;
    }

    char id[WORKFLOW_JOB_ID_LEN];
    wf_gen_uuid(id);

    time_t now    = time(NULL);
    time_t run_at = now + (time_t)delay_sec;

    pthread_mutex_lock(&s_db_mutex);

    sqlite3_stmt *stmt = NULL;
    const char *sql =
        "INSERT INTO jobs (id, type, payload, status, retry_count, run_at, created_at)"
        " VALUES (?, ?, ?, 'pending', 0, ?, ?);";

    int rc = sqlite3_prepare_v2(s_db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[workflow] prepare insert: %s\n", sqlite3_errmsg(s_db));
        pthread_mutex_unlock(&s_db_mutex);
        return -1;
    }

    sqlite3_bind_text (stmt, 1, id,      -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 2, type,    -1, SQLITE_STATIC);
    sqlite3_bind_text (stmt, 3, payload, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)run_at);
    sqlite3_bind_int64(stmt, 5, (sqlite3_int64)now);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&s_db_mutex);

    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[workflow] insert failed: %s\n", sqlite3_errmsg(s_db));
        return -1;
    }

    if (out_id) strncpy(out_id, id, WORKFLOW_JOB_ID_LEN);
    fprintf(stderr, "[workflow] enqueued %s type=%s delay=%ds\n", id, type, delay_sec);
    return 0;
}

int workflow_get_job(const char *id, job_t *out)
{
    if (!s_db || !out) return -1;

    pthread_mutex_lock(&s_db_mutex);

    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(s_db,
        "SELECT id, type, payload, status, retry_count, run_at, created_at"
        " FROM jobs WHERE id=? LIMIT 1;",
        -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);

    int found = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        strncpy(out->id,   (const char *)sqlite3_column_text(stmt, 0), WORKFLOW_JOB_ID_LEN - 1);
        strncpy(out->type, (const char *)sqlite3_column_text(stmt, 1), WORKFLOW_TYPE_MAX   - 1);
        const char *p = (const char *)sqlite3_column_text(stmt, 2);
        out->payload = p ? strdup(p) : strdup("{}");

        const char *status_str = (const char *)sqlite3_column_text(stmt, 3);
        if      (!strcmp(status_str, "pending")) out->status = JOB_PENDING;
        else if (!strcmp(status_str, "running")) out->status = JOB_RUNNING;
        else if (!strcmp(status_str, "done"))    out->status = JOB_DONE;
        else                                     out->status = JOB_FAILED;

        out->retry_count = sqlite3_column_int  (stmt, 4);
        out->run_at      = (time_t)sqlite3_column_int64(stmt, 5);
        out->created_at  = (time_t)sqlite3_column_int64(stmt, 6);
        found = 1;
    }

    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&s_db_mutex);

    return found ? 0 : -1;
}

int workflow_cancel_job(const char *id)
{
    if (!s_db) return -1;

    pthread_mutex_lock(&s_db_mutex);
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(s_db,
        "UPDATE jobs SET status='failed' WHERE id=? AND status='pending';",
        -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    int changed = sqlite3_changes(s_db);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&s_db_mutex);

    return changed > 0 ? 0 : -1;
}

int workflow_purge_done(int max_age_seconds)
{
    if (!s_db) return -1;

    time_t cutoff = time(NULL) - (time_t)max_age_seconds;

    pthread_mutex_lock(&s_db_mutex);
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(s_db,
        "DELETE FROM jobs WHERE status IN ('done','failed') AND created_at < ?;",
        -1, &stmt, NULL);
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)cutoff);
    sqlite3_step(stmt);
    int deleted = sqlite3_changes(s_db);
    sqlite3_finalize(stmt);
    pthread_mutex_unlock(&s_db_mutex);

    fprintf(stderr, "[workflow] purged %d old jobs\n", deleted);
    return deleted;
}
