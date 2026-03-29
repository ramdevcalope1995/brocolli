/*
 * brocolli - agent.c
 *
 * Implements a C-based bridge to the QuantClaw WebSocket RPC gateway.
 *
 * This allows "brocolli" to delegate high-level reasoning to
 * QuantClaw while providing the sandbox and browser as tools.
 */

#include "agent.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

#include <kore/kore.h>
#include <kore/http.h>
#include "../vendor/cjson/cJSON.h"

/* ------------------------------------------------------------------
 * Internal globals
 * ------------------------------------------------------------------ */

#define AGENT_MAX_TASKS 32
static agent_task_t s_tasks[AGENT_MAX_TASKS];
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static int s_initialised = 0;

/* ------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------ */

static void gen_uuid(char buf[AGENT_TASK_ID_LEN])
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
    snprintf(buf, AGENT_TASK_ID_LEN,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0],b[1],b[2],b[3], b[4],b[5], b[6],b[7],
        b[8],b[9], b[10],b[11],b[12],b[13],b[14],b[15]);
}

/* ------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------ */

int agent_module_init(void)
{
    if (s_initialised) return 0;
    memset(s_tasks, 0, sizeof(s_tasks));
    for (int i = 0; i < AGENT_MAX_TASKS; i++) {
        s_tasks[i].ws_handle = NULL;
    }
    s_initialised = 1;
    return 0;
}

agent_task_t *agent_task_create(const char *goal)
{
    if (!s_initialised) return NULL;

    pthread_mutex_lock(&s_mutex);
    int slot = -1;
    for (int i = 0; i < AGENT_MAX_TASKS; i++) {
        if (s_tasks[i].ws_handle == NULL) { slot = i; break; }
    }
    if (slot < 0) { pthread_mutex_unlock(&s_mutex); return NULL; }
    pthread_mutex_unlock(&s_mutex);

    char id[AGENT_TASK_ID_LEN];
    gen_uuid(id);

    /* 
     * TODO: Implement WebSocket connection to QuantClaw gateway (port 18800).
     * For now, we log the agentic task creation.
     */
    kore_log(LOG_INFO, "agent[%s]: starting task with goal: %s", id, goal);
    
    /* 
     * In a real implementation, we would:
     * 1. Connect to ws://127.0.0.1:18800/gateway
     * 2. Send a JSON RPC request: {"id":1, "method":"agent.task", "params":{"goal":"..."}}
     * 3. Register our sandbox and browser as skills in the QuantClaw workspace.
     */
    
    pthread_mutex_lock(&s_mutex);
    strncpy(s_tasks[slot].id, id, AGENT_TASK_ID_LEN);
    s_tasks[slot].ws_handle = (void *)1; /* Placeholder for active task */
    pthread_mutex_unlock(&s_mutex);

    return &s_tasks[slot];
}

int agent_task_status(agent_task_t *t, char *out_buf, size_t out_len)
{
    if (!t || t->ws_handle == NULL) return -1;
    kore_log(LOG_INFO, "agent[%s]: checking task status", t->id);
    /* JSON RPC: agent.status */
    return 0;
}

int agent_task_cancel(agent_task_t *t)
{
    if (!t || t->ws_handle == NULL) return -1;

    kore_log(LOG_INFO, "agent[%s]: cancelling task", t->id);
    /* JSON RPC: agent.interrupt */

    pthread_mutex_lock(&s_mutex);
    t->ws_handle = NULL;
    memset(t->id, 0, AGENT_TASK_ID_LEN);
    pthread_mutex_unlock(&s_mutex);

    return 0;
}
