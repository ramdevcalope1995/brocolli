/*
 * brocolli - sandbox.c
 *
 * How it works (three-step mental model):
 *
 *   1. sandbox_create()
 *      Forks a "namespace holder" process using clone(2) with namespace
 *      flags. The holder does nothing — it just calls pause() forever.
 *      Its only job is to keep the namespaces alive. We open file
 *      descriptors into /proc/<pid>/ns/* so we can re-enter those
 *      namespaces at any time, even from unrelated threads.
 *
 *   2. sandbox_exec()
 *      Forks a new worker child in the *calling* process's namespace.
 *      The worker calls setns(2) on each namespace fd to enter the
 *      sandbox's namespaces, then applies a seccomp filter, then
 *      execs the target binary. We pipe stdout/stderr back to the
 *      caller, kill on timeout.
 *
 *   3. sandbox_destroy()
 *      Sends SIGKILL to the holder, closes all namespace fds, frees
 *      the slot. All previously exec'd children in that namespace
 *      lose their PID namespace and are also killed by the kernel.
 *
 * Threading: a single pthread_mutex protects the sandbox table.
 * The mutex is held only while reading/writing the table, not during
 * blocking operations like waitpid() or read().
 *
 * Persistence: Sandbox metadata is stored in SQLite to survive restarts.
 */

#define _GNU_SOURCE  /* for clone(2), setns(2), unshare(2) */

#include "sandbox.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sched.h>

#include <seccomp.h>   /* libseccomp — apt install libseccomp-dev */
#include "../vendor/sqlite3/sqlite3.h"

/* ------------------------------------------------------------------
 * Internal globals
 * ------------------------------------------------------------------ */

static sandbox_t      s_table[SANDBOX_MAX];
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static int            s_initialised = 0;
static sqlite3       *s_db = NULL;

/* ------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------ */

/*
 * gen_uuid() — build a RFC-4122 UUID v4 from /dev/urandom.
 * Writes SANDBOX_ID_LEN bytes (including null terminator) into buf.
 */
static void gen_uuid(char buf[SANDBOX_ID_LEN])
{
    unsigned char b[16];
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0 || read(fd, b, 16) != 16) {
        /* fallback: use time + pid (weaker but won't crash) */
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        unsigned long v = (unsigned long)ts.tv_nsec ^ ((unsigned long)getpid() << 32);
        memset(b, 0, 16);
        memcpy(b, &v, sizeof(v));
    }
    if (fd >= 0) close(fd);

    /* version 4, variant bits */
    b[6] = (b[6] & 0x0f) | 0x40;
    b[8] = (b[8] & 0x3f) | 0x80;

    snprintf(buf, SANDBOX_ID_LEN,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0],b[1],b[2],b[3], b[4],b[5], b[6],b[7],
        b[8],b[9], b[10],b[11],b[12],b[13],b[14],b[15]);
}

/*
 * open_ns_fd() — open /proc/<pid>/ns/<name>, return fd or -1.
 */
static int open_ns_fd(pid_t pid, const char *ns_name)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/ns/%s", (int)pid, ns_name);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "[sandbox] open_ns_fd: %s: %s\n", path, strerror(errno));
    }
    return fd;
}

/*
 * apply_seccomp() — install a whitelist seccomp filter.
 */
static int apply_seccomp(void)
{
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_ERRNO(EPERM));
    if (!ctx) {
        fprintf(stderr, "[sandbox] seccomp_init failed\n");
        return -1;
    }

    /* memory */
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(brk),         0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mmap),        0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(munmap),      0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mprotect),    0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(mremap),      0);

    /* I/O */
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(read),        0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(write),       0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(readv),       0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(writev),      0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(pread64),     0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(pwrite64),    0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(lseek),       0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(ioctl),       0);

    /* file ops */
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(open),        0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(openat),      0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(close),       0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(stat),        0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fstat),       0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(lstat),       0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(newfstatat),  0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(access),      0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(faccessat),   0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(readlink),    0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(readlinkat),  0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getdents64),  0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(dup),         0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(dup2),        0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(dup3),        0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(pipe),        0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(pipe2),       0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(fcntl),       0);

    /* process/thread lifecycle */
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(execve),      0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit),        0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(exit_group),  0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(wait4),       0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(waitid),      0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getpid),      0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getppid),     0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getuid),      0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getgid),      0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(geteuid),     0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(getegid),     0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(kill),        0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(signal),      0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(sigaction),   0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigaction),0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigreturn),0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(rt_sigprocmask),0);

    /* time */
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(clock_gettime), 0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(gettimeofday),  0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(nanosleep),     0);

    /* system info */
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(uname),         0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(arch_prctl),    0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(set_robust_list),0);
    seccomp_rule_add(ctx, SCMP_ACT_ALLOW, SCMP_SYS(futex),         0);

    int rc = seccomp_load(ctx);
    seccomp_release(ctx);

    if (rc != 0) {
        fprintf(stderr, "[sandbox] seccomp_load failed: %s\n", strerror(-rc));
        return -1;
    }
    return 0;
}

typedef struct {
    int         net_enabled;
    long        mem_limit_bytes;
    char        hostname[65];
    int         ready_pipe_wr;
} holder_args_t;

#define HOLDER_STACK_SIZE (512 * 1024)

static int ns_holder_fn(void *arg)
{
    holder_args_t *a = (holder_args_t *)arg;
    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NOEXEC | MS_NODEV, NULL) != 0) {}
    if (a->hostname[0]) { sethostname(a->hostname, strlen(a->hostname)); }
    if (a->mem_limit_bytes > 0) {
        struct rlimit rl = { .rlim_cur = (rlim_t)a->mem_limit_bytes, .rlim_max = (rlim_t)a->mem_limit_bytes };
        setrlimit(RLIMIT_AS, &rl);
    }
    char one = 1;
    write(a->ready_pipe_wr, &one, 1);
    close(a->ready_pipe_wr);
    for (;;) pause();
    return 0;
}

/* ------------------------------------------------------------------
 * DB Persistence
 * ------------------------------------------------------------------ */

static int db_exec(const char *sql)
{
    char *errmsg = NULL;
    int rc = sqlite3_exec(s_db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[sandbox] db_exec error: %s\n", errmsg ? errmsg : "?");
        sqlite3_free(errmsg);
        return -1;
    }
    return 0;
}

static int db_init(void)
{
    int rc = sqlite3_open("brocolli_sandboxes.db", &s_db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[sandbox] sqlite3_open failed: %s\n", sqlite3_errmsg(s_db));
        return -1;
    }
    db_exec("PRAGMA journal_mode=WAL;");
    const char *sql = "CREATE TABLE IF NOT EXISTS sandboxes ("
                      "  id           TEXT PRIMARY KEY,"
                      "  ns_pid       INTEGER NOT NULL,"
                      "  net_enabled  INTEGER NOT NULL"
                      ");";
    return db_exec(sql);
}

static int db_save(const char *id, pid_t pid, int net)
{
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(s_db, "INSERT INTO sandboxes (id, ns_pid, net_enabled) VALUES (?, ?, ?);", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, (int)pid);
    sqlite3_bind_int(stmt, 3, net);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

static int db_remove(const char *id)
{
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(s_db, "DELETE FROM sandboxes WHERE id=?;", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

static void db_load_all(void)
{
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(s_db, "SELECT id, ns_pid, net_enabled FROM sandboxes;", -1, &stmt, NULL);
    int i = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW && i < SANDBOX_MAX) {
        const char *id = (const char *)sqlite3_column_text(stmt, 0);
        pid_t pid = (pid_t)sqlite3_column_int(stmt, 1);
        int net = sqlite3_column_int(stmt, 2);

        /* Verify if process still exists */
        if (kill(pid, 0) == 0) {
            strncpy(s_table[i].id, id, SANDBOX_ID_LEN);
            s_table[i].ns_pid = pid;
            s_table[i].ns_pid_fd = open_ns_fd(pid, "pid");
            s_table[i].ns_net_fd = open_ns_fd(pid, "net");
            s_table[i].ns_mnt_fd = open_ns_fd(pid, "mnt");
            s_table[i].ns_uts_fd = open_ns_fd(pid, "uts");
            s_table[i].ns_ipc_fd = open_ns_fd(pid, "ipc");
            s_table[i].net_enabled = net;
            s_table[i].state = SANDBOX_READY;
            i++;
        } else {
            /* Process gone, clean up DB */
            db_remove(id);
        }
    }
    sqlite3_finalize(stmt);
}

/* ------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------ */

int sandbox_module_init(void)
{
    if (s_initialised) return 0;
    memset(s_table, 0, sizeof(s_table));
    for (int i = 0; i < SANDBOX_MAX; i++) {
        s_table[i].ns_pid = -1;
        s_table[i].state = SANDBOX_STOPPED;
    }
    if (db_init() != 0) return -1;
    db_load_all();
    s_initialised = 1;
    return 0;
}

void sandbox_module_shutdown(void)
{
    pthread_mutex_lock(&s_mutex);
    for (int i = 0; i < SANDBOX_MAX; i++) {
        if (s_table[i].state == SANDBOX_READY && s_table[i].ns_pid > 0) {
            if (s_table[i].ns_pid_fd >= 0) close(s_table[i].ns_pid_fd);
            if (s_table[i].ns_net_fd >= 0) close(s_table[i].ns_net_fd);
            if (s_table[i].ns_mnt_fd >= 0) close(s_table[i].ns_mnt_fd);
            if (s_table[i].ns_uts_fd >= 0) close(s_table[i].ns_uts_fd);
            if (s_table[i].ns_ipc_fd >= 0) close(s_table[i].ns_ipc_fd);
        }
    }
    if (s_db) sqlite3_close(s_db);
    pthread_mutex_unlock(&s_mutex);
}

int sandbox_create(const sandbox_config_t *cfg, char out_id[SANDBOX_ID_LEN])
{
    if (!s_initialised) return -1;
    pthread_mutex_lock(&s_mutex);
    int slot = -1;
    for (int i = 0; i < SANDBOX_MAX; i++) {
        if (s_table[i].state == SANDBOX_STOPPED) { slot = i; break; }
    }
    if (slot < 0) { pthread_mutex_unlock(&s_mutex); errno = ENOSPC; return -1; }
    pthread_mutex_unlock(&s_mutex);

    holder_args_t args;
    memset(&args, 0, sizeof(args));
    args.net_enabled = cfg ? cfg->net_enabled : 0;
    args.mem_limit_bytes = cfg ? cfg->mem_limit_bytes : 0;
    char new_id[SANDBOX_ID_LEN];
    gen_uuid(new_id);
    snprintf(args.hostname, sizeof(args.hostname), "brocolli-%.8s", new_id);
    if (cfg && cfg->hostname) { strncpy(args.hostname, cfg->hostname, 64); args.hostname[64] = '\0'; }

    int pipe_fds[2];
    if (pipe2(pipe_fds, O_CLOEXEC) < 0) return -1;
    args.ready_pipe_wr = pipe_fds[1];
    char *stack = malloc(HOLDER_STACK_SIZE);
    if (!stack) { close(pipe_fds[0]); close(pipe_fds[1]); return -1; }

    int clone_flags = SIGCHLD | CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC;
    if (!args.net_enabled) clone_flags |= CLONE_NEWNET;

    pid_t holder_pid = clone(ns_holder_fn, stack + HOLDER_STACK_SIZE, clone_flags, &args);
    free(stack);
    if (holder_pid < 0) { close(pipe_fds[0]); close(pipe_fds[1]); return -1; }

    close(pipe_fds[1]);
    char ready = 0;
    if (read(pipe_fds[0], &ready, 1) != 1 || ready != 1) {
        kill(holder_pid, SIGKILL); waitpid(holder_pid, NULL, 0); close(pipe_fds[0]); return -1;
    }
    close(pipe_fds[0]);

    int pfd = open_ns_fd(holder_pid, "pid");
    int nfd = open_ns_fd(holder_pid, "net");
    int mfd = open_ns_fd(holder_pid, "mnt");
    int ufd = open_ns_fd(holder_pid, "uts");
    int ifd = open_ns_fd(holder_pid, "ipc");

    pthread_mutex_lock(&s_mutex);
    strncpy(s_table[slot].id, new_id, SANDBOX_ID_LEN);
    s_table[slot].ns_pid = holder_pid;
    s_table[slot].ns_pid_fd = pfd;
    s_table[slot].ns_net_fd = nfd;
    s_table[slot].ns_mnt_fd = mfd;
    s_table[slot].ns_uts_fd = ufd;
    s_table[slot].ns_ipc_fd = ifd;
    s_table[slot].state = SANDBOX_READY;
    s_table[slot].net_enabled = args.net_enabled;
    db_save(new_id, holder_pid, args.net_enabled);
    pthread_mutex_unlock(&s_mutex);

    strncpy(out_id, new_id, SANDBOX_ID_LEN);
    return 0;
}

int sandbox_exec(const char *id, const char *path, char * const argv[], char *out_buf, size_t out_buf_len, int *exit_code)
{
    pthread_mutex_lock(&s_mutex);
    sandbox_t snap;
    int found = 0;
    for (int i = 0; i < SANDBOX_MAX; i++) {
        if (s_table[i].state == SANDBOX_READY && strncmp(s_table[i].id, id, SANDBOX_ID_LEN) == 0) {
            snap = s_table[i]; found = 1; break;
        }
    }
    pthread_mutex_unlock(&s_mutex);
    if (!found) { errno = ENOENT; return -1; }

    int out_pipe[2];
    if (pipe2(out_pipe, O_CLOEXEC) < 0) return -1;
    pid_t worker = fork();
    if (worker < 0) { close(out_pipe[0]); close(out_pipe[1]); return -1; }

    if (worker == 0) {
        dup2(out_pipe[1], STDOUT_FILENO); dup2(out_pipe[1], STDERR_FILENO);
        close(out_pipe[0]); close(out_pipe[1]);
        if (setns(snap.ns_mnt_fd, CLONE_NEWNS) != 0) _exit(127);
        if (setns(snap.ns_uts_fd, CLONE_NEWUTS) != 0) _exit(127);
        if (setns(snap.ns_ipc_fd, CLONE_NEWIPC) != 0) _exit(127);
        if (snap.ns_net_fd >= 0 && !snap.net_enabled) { if (setns(snap.ns_net_fd, CLONE_NEWNET) != 0) _exit(127); }
        if (setns(snap.ns_pid_fd, CLONE_NEWPID) != 0) _exit(127);
        pid_t inner = fork();
        if (inner < 0) _exit(127);
        if (inner > 0) { int st; waitpid(inner, &st, 0); _exit(WIFEXITED(st) ? WEXITSTATUS(st) : 126); }
        if (apply_seccomp() != 0) _exit(126);
        execv(path, argv); _exit(127);
    }

    close(out_pipe[1]);
    size_t total = 0;
    ssize_t n;
    char tmp[4096];
    alarm(SANDBOX_EXEC_TIMEOUT_SEC);
    while ((n = read(out_pipe[0], tmp, sizeof(tmp))) > 0) {
        if (out_buf && total + (size_t)n < out_buf_len) { memcpy(out_buf + total, tmp, n); }
        total += n;
    }
    alarm(0); close(out_pipe[0]);
    if (out_buf && out_buf_len > 0) { out_buf[total < out_buf_len ? total : out_buf_len - 1] = '\0'; }
    int status;
    if (waitpid(worker, &status, 0) < 0) {
        if (errno == EINTR) { kill(worker, SIGKILL); waitpid(worker, NULL, 0); errno = ETIMEDOUT; return -1; }
        return -1;
    }
    if (exit_code) *exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return 0;
}

int sandbox_destroy(const char *id)
{
    pthread_mutex_lock(&s_mutex);
    int found = -1;
    for (int i = 0; i < SANDBOX_MAX; i++) {
        if (s_table[i].state == SANDBOX_READY && strncmp(s_table[i].id, id, SANDBOX_ID_LEN) == 0) {
            found = i; break;
        }
    }
    if (found < 0) { pthread_mutex_unlock(&s_mutex); errno = ENOENT; return -1; }

    sandbox_t *s = &s_table[found];
    pid_t pid = s->ns_pid;
    if (s->ns_pid_fd >= 0) close(s->ns_pid_fd);
    if (s->ns_net_fd >= 0) close(s->ns_net_fd);
    if (s->ns_mnt_fd >= 0) close(s->ns_mnt_fd);
    if (s->ns_uts_fd >= 0) close(s->ns_uts_fd);
    if (s->ns_ipc_fd >= 0) close(s->ns_ipc_fd);
    s->state = SANDBOX_STOPPED;
    s->ns_pid = -1;
    db_remove(id);
    pthread_mutex_unlock(&s_mutex);

    if (pid > 0) { kill(pid, SIGKILL); waitpid(pid, NULL, 0); }
    return 0;
}

const sandbox_t *sandbox_find(const char *id)
{
    pthread_mutex_lock(&s_mutex);
    for (int i = 0; i < SANDBOX_MAX; i++) {
        if (s_table[i].state == SANDBOX_READY && strncmp(s_table[i].id, id, SANDBOX_ID_LEN) == 0) {
            pthread_mutex_unlock(&s_mutex); return &s_table[i];
        }
    }
    pthread_mutex_unlock(&s_mutex);
    return NULL;
}
