#pragma once

/*
 * brocolli - agent engine (QuantClaw Bridge)
 *
 * Provides a C interface to communicate with the QuantClaw
 * WebSocket RPC gateway (port 18800).
 *
 * This allows "brocolli" to delegate high-level reasoning to
 * QuantClaw while providing the sandbox and browser as tools.
 */

#include <stddef.h>

/* ------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------ */

#define AGENT_TASK_ID_LEN   37
#define AGENT_GATEWAY_PORT  18800
#define AGENT_GATEWAY_HOST  "127.0.0.1"

/* ------------------------------------------------------------------
 * Types
 * ------------------------------------------------------------------ */

typedef struct {
    char    id[AGENT_TASK_ID_LEN];
    void    *ws_handle; /* Internal WebSocket handle to QuantClaw */
} agent_task_t;

/* ------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------ */

/*
 * agent_module_init()
 *   Initialise the agent engine.
 */
int  agent_module_init(void);

/*
 * agent_task_create()
 *   Send a high-level goal to the QuantClaw agent.
 *   Returns a pointer to the task handle or NULL on error.
 */
agent_task_t *agent_task_create(const char *goal);

/*
 * agent_task_status()
 *   Get the current status/transcript of an agent task.
 */
int  agent_task_status(agent_task_t *t, char *out_buf, size_t out_len);

/*
 * agent_task_cancel()
 *   Interrupt and stop an ongoing agent task.
 */
int  agent_task_cancel(agent_task_t *t);
