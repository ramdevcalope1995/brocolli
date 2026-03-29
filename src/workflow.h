#pragma once

/*
 * brocolli - workflow engine
 *
 * An Inngest-inspired durable job queue backed by SQLite3.
 *
 * Concepts:
 *   - Job types are strings (e.g. "browser.navigate", "llm.infer").
 *   - Each job type has a registered C handler function.
 *   - Jobs are enqueued with a JSON payload and stored in SQLite.
 *   - A background worker thread picks up PENDING jobs and calls
 *     the matching handler.
 *   - On handler failure, the job is retried with exponential backoff
 *     up to WORKFLOW_MAX_RETRIES attempts, then marked FAILED.
 *
 * Dependencies:
 *   sqlite3 amalgamation (vendor/sqlite3/sqlite3.c + sqlite3.h)
 *
 * Thread safety:
 *   All public functions are safe to call from multiple threads.
 *   The SQLite connection uses WAL mode + a mutex.
 */

#include <stddef.h>
#include <time.h>

/* ------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------ */

#define WORKFLOW_JOB_ID_LEN    37    /* UUID string length incl. NUL */
#define WORKFLOW_TYPE_MAX      64    /* max job type string length    */
#define WORKFLOW_PAYLOAD_MAX   65536 /* max JSON payload size: 64 KB  */
#define WORKFLOW_MAX_RETRIES   5     /* give up after this many fails  */
#define WORKFLOW_POLL_MS       500   /* worker polls every 500 ms      */

/* Backoff: retry_delay_seconds = WORKFLOW_BACKOFF_BASE ^ retry_count */
#define WORKFLOW_BACKOFF_BASE  3     /* 3, 9, 27, 81, 243 seconds      */

/* ------------------------------------------------------------------
 * Types
 * ------------------------------------------------------------------ */

typedef enum {
    JOB_PENDING = 0,
    JOB_RUNNING = 1,
    JOB_DONE    = 2,
    JOB_FAILED  = 3
} job_status_t;

/*
 * workflow_handler_t — callback signature for job handlers.
 *
 *   job_id   : UUID string of the job being processed.
 *   job_type : type string (same as registered with workflow_register).
 *   payload  : null-terminated JSON string from when job was enqueued.
 *
 * Return 0 to mark the job DONE.
 * Return non-zero to mark it for retry (or FAILED if retries exhausted).
 */
typedef int (*workflow_handler_t)(const char *job_id,
                                  const char *job_type,
                                  const char *payload);

/*
 * job_t — a snapshot of a job row. Returned by workflow_get_job().
 */
typedef struct {
    char         id[WORKFLOW_JOB_ID_LEN];
    char         type[WORKFLOW_TYPE_MAX];
    char        *payload;        /* heap-allocated; caller must free()   */
    job_status_t status;
    int          retry_count;
    time_t       run_at;
    time_t       created_at;
} job_t;

/* ------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------ */

/*
 * workflow_init()
 *   Open (or create) the SQLite database at db_path, create the jobs
 *   table if it doesn't exist, then start the worker thread.
 *   Must be called once before any other workflow_* function.
 *
 *   db_path: file path, e.g. "/var/lib/brocolli/jobs.db"
 *            or ":memory:" for an in-process test database.
 *
 *   Returns 0 on success, -1 on error.
 */
int workflow_init(const char *db_path);

/*
 * workflow_shutdown()
 *   Signal the worker thread to stop, wait for it to exit, close DB.
 *   Call at process exit.
 */
void workflow_shutdown(void);

/*
 * workflow_register()
 *   Associate a handler function with a job type string.
 *   Must be called before workflow_init() starts the worker (or at
 *   least before any jobs of this type are enqueued).
 *
 *   Returns 0 on success, -1 if the handler table is full.
 */
int workflow_register(const char *type, workflow_handler_t handler);

/*
 * workflow_enqueue()
 *   Insert a new PENDING job into the queue.
 *
 *   type     : job type string (must have a registered handler).
 *   payload  : arbitrary JSON string describing the work.
 *   delay_sec: how many seconds to wait before the job becomes
 *              eligible for pickup (0 = run immediately).
 *   out_id   : if non-NULL, receives the new job's UUID.
 *
 *   Returns 0 on success, -1 on error.
 */
int workflow_enqueue(const char *type,
                     const char *payload,
                     int         delay_sec,
                     char        out_id[WORKFLOW_JOB_ID_LEN]);

/*
 * workflow_get_job()
 *   Retrieve a snapshot of a job by its UUID.
 *   Fills *out and allocates out->payload on the heap.
 *   Caller must free(out->payload) when done.
 *
 *   Returns 0 if found, -1 if not found or error.
 */
int workflow_get_job(const char *id, job_t *out);

/*
 * workflow_cancel_job()
 *   Mark a PENDING job as FAILED without running it.
 *   Has no effect on jobs that are already RUNNING, DONE, or FAILED.
 *
 *   Returns 0 on success, -1 on error.
 */
int workflow_cancel_job(const char *id);

/*
 * workflow_purge_done()
 *   Delete all DONE and FAILED jobs older than max_age_seconds.
 *   Returns number of rows deleted, -1 on error.
 */
int workflow_purge_done(int max_age_seconds);
