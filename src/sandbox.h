#pragma once

/*
 * brocolli - sandbox engine
 *
 * Provides isolated execution environments using Linux namespaces
 * (PID, mount, network, UTS, IPC) and seccomp syscall filtering.
 *
 * Each sandbox is a long-lived "namespace holder" process that we
 * enter on demand via setns(2) whenever sandbox_exec() is called.
 *
 * Dependencies:
 *   libseccomp  (-lseccomp)  -- apt install libseccomp-dev
 *
 * Linux-only. Requires CAP_SYS_ADMIN (run as root or with user namespaces
 * enabled: echo 1 > /proc/sys/kernel/unprivileged_userns_clone)
 */

#include <sys/types.h>
#include <stddef.h>

/* ------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------ */

#define SANDBOX_ID_LEN    37          /* "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx\0" */
#define SANDBOX_MAX       32          /* max concurrent sandboxes */
#define SANDBOX_EXEC_TIMEOUT_SEC 30   /* kill exec'd child after this many seconds */
#define SANDBOX_OUT_MAX   (256*1024)  /* max captured output: 256 KB */

/* ------------------------------------------------------------------
 * Types
 * ------------------------------------------------------------------ */

typedef enum {
    SANDBOX_READY   = 0,
    SANDBOX_STOPPED = 1,
    SANDBOX_ERROR   = 2
} sandbox_state_t;

/*
 * sandbox_t - internal handle. Treat as opaque outside sandbox.c.
 * The ns_*_fd fields are open file descriptors into
 *   /proc/<ns_pid>/ns/{pid,net,mnt,uts,ipc}
 * As long as these fds stay open the namespaces survive even if the
 * holder process is replaced.
 */
typedef struct {
    char            id[SANDBOX_ID_LEN];
    pid_t           ns_pid;      /* the "holder" process sleeping in the NS */
    int             ns_pid_fd;   /* fd: /proc/<pid>/ns/pid  */
    int             ns_net_fd;   /* fd: /proc/<pid>/ns/net  */
    int             ns_mnt_fd;   /* fd: /proc/<pid>/ns/mnt  */
    int             ns_uts_fd;   /* fd: /proc/<pid>/ns/uts  */
    int             ns_ipc_fd;   /* fd: /proc/<pid>/ns/ipc  */
    sandbox_state_t state;
    int             net_enabled; /* was network kept when creating? */
} sandbox_t;

/*
 * sandbox_config_t - options passed to sandbox_create().
 * Zero-initialise the struct before filling fields you care about.
 *
 *   net_enabled:      0 = isolated loopback only (default / safer)
 *                     1 = share host network namespace
 *   mem_limit_bytes:  0 = no limit
 *                     N = set RLIMIT_AS to N bytes
 *   hostname:         NULL = keep "brocolli-<id>" default
 */
typedef struct {
    int         net_enabled;
    long        mem_limit_bytes;
    const char *hostname;        /* optional, max 64 chars */
} sandbox_config_t;

/* ------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------ */

/*
 * sandbox_module_init()
 *   Call once at process start. Initialises the sandbox table mutex.
 *   Returns 0 on success, -1 on error.
 */
int  sandbox_module_init(void);

/*
 * sandbox_module_shutdown()
 *   Destroy all live sandboxes and release resources.
 *   Call at process exit.
 */
void sandbox_module_shutdown(void);

/*
 * sandbox_create()
 *   Create a new sandbox and write its UUID into out_id.
 *   out_id must point to at least SANDBOX_ID_LEN bytes.
 *   Returns 0 on success, -1 on error (check errno).
 */
int  sandbox_create(const sandbox_config_t *cfg,
                    char out_id[SANDBOX_ID_LEN]);

/*
 * sandbox_exec()
 *   Run path+argv inside the sandbox identified by id.
 *   stdout+stderr are captured into out_buf (null-terminated).
 *   *exit_code is set to the child's exit status.
 *
 *   out_buf / out_buf_len may be NULL/0 to discard output.
 *   Returns 0 on success, -1 on error (timeout → ETIMEDOUT).
 */
int  sandbox_exec(const char   *id,
                  const char   *path,
                  char * const  argv[],
                  char         *out_buf,
                  size_t        out_buf_len,
                  int          *exit_code);

/*
 * sandbox_destroy()
 *   Kill the namespace holder, close namespace fds, free the slot.
 *   Returns 0 on success, -1 if id not found.
 */
int  sandbox_destroy(const char *id);

/*
 * sandbox_find()
 *   Returns a pointer to the sandbox with the given id, or NULL.
 *   The table mutex is NOT held on return — copy what you need.
 */
const sandbox_t *sandbox_find(const char *id);
