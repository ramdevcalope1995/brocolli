/*
 * brocolli - brocolli.c
 *
 * Kore application entry point.
 *
 * Registers HTTP routes and wires the sandbox + workflow engines.
 * Add this file as the main Kore module source.
 *
 * Routes exposed:
 *
 *   POST   /api/sandbox            create a new sandbox
 *   POST   /api/sandbox/:id/exec   exec a command inside a sandbox
 *   DELETE /api/sandbox/:id        destroy a sandbox
 *   GET    /api/sandbox/:id        inspect a sandbox
 *
 *   POST   /api/jobs               enqueue a job
 *   GET    /api/jobs/:id           get job status
 *   DELETE /api/jobs/:id           cancel a pending job
 *
 * Build: see Makefile — links against libseccomp, sqlite3, pthread.
 */

#include <kore/kore.h>
#include <kore/http.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sandbox.h"
#include "workflow.h"
#include "browser.h"
#include "agent.h"
#include "../vendor/cjson/cJSON.h"

/* ------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------ */

/* Send a JSON response.  status: HTTP status code, body: JSON string. */
static void json_reply(struct http_request *req, int status, const char *body)
{
    http_response_header(req, "content-type", "application/json");
    http_response(req, status, body, strlen(body));
}

/* Build a quick {"error":"..."} JSON string into buf. */
static void make_error(char *buf, size_t len, const char *msg)
{
    snprintf(buf, len, "{\"error\":\"%s\"}", msg);
}

/* ------------------------------------------------------------------
 * Example workflow handlers (register your real handlers here)
 * ------------------------------------------------------------------ */

static int handle_sandbox_job(const char *job_id,
                               const char *job_type,
                               const char *payload)
{
    (void)job_type;
    /*
     * Example: payload is {"sandbox_id":"...","cmd":"/bin/echo","args":["hello"]}
     * Real implementation would parse the JSON and call sandbox_exec().
     * For now, just log and succeed.
     */
    kore_log(LOG_INFO, "workflow: running sandbox job %s payload=%s",
             job_id, payload);
    return 0; /* 0 = success */
}

/* ------------------------------------------------------------------
 * Kore module init / cleanup
 * ------------------------------------------------------------------ */

void kore_parent_configure(int argc, char *argv[])
{
    (void)argc; (void)argv;

    /* sandbox engine */
    if (sandbox_module_init() != 0)
        kore_fatal("sandbox_module_init failed");

    /* browser engine */
    if (browser_module_init() != 0)
        kore_fatal("browser_module_init failed");

    /* agent engine (QuantClaw bridge) */
    if (agent_module_init() != 0)
        kore_fatal("agent_module_init failed");

    /* workflow engine — database stored next to the binary */
    if (workflow_init("brocolli_jobs.db") != 0)
        kore_fatal("workflow_init failed");

    /* register job type handlers */
    workflow_register("sandbox.exec", handle_sandbox_job);

    kore_log(LOG_INFO, "brocolli engines initialised");
}

void kore_parent_teardown(void)
{
    workflow_shutdown();
    sandbox_module_shutdown();
}

/* ------------------------------------------------------------------
 * Route: POST /api/sandbox
 *
 * Body (JSON):
 *   { "net_enabled": 0, "mem_limit_mb": 128, "hostname": "mybox" }
 *
 * Response 201:
 *   { "id": "<uuid>" }
 * ------------------------------------------------------------------ */
int route_sandbox_create(struct http_request *req)
{
    if (req->method != HTTP_METHOD_POST) {
        json_reply(req, 405, "{\"error\":\"method not allowed\"}");
        return KORE_RESULT_OK;
    }

    sandbox_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.net_enabled      = 0;
    cfg.mem_limit_bytes  = 128L * 1024 * 1024; /* 128 MB default */

    /* Parse request body */
    struct kore_buf *buf = kore_buf_alloc(req->http_body->length + 1);
    kore_buf_append(buf, req->http_body->data, req->http_body->length);
    kore_buf_append(buf, "\0", 1);
    
    cJSON *root = cJSON_Parse((const char *)buf->data);
    kore_buf_free(buf);

    if (root) {
        cJSON *net = cJSON_GetObjectItem(root, "net_enabled");
        if (cJSON_IsBool(net)) cfg.net_enabled = cJSON_IsTrue(net);
        else if (cJSON_IsNumber(net)) cfg.net_enabled = net->valueint;

        cJSON *mem = cJSON_GetObjectItem(root, "mem_limit_mb");
        if (cJSON_IsNumber(mem)) cfg.mem_limit_bytes = (long)mem->valuedouble * 1024 * 1024;

        cJSON *host = cJSON_GetObjectItem(root, "hostname");
        if (cJSON_IsString(host)) cfg.hostname = host->valuestring;
    }

    char id[SANDBOX_ID_LEN];
    if (sandbox_create(&cfg, id) != 0) {
        if (root) cJSON_Delete(root);
        char err[128];
        make_error(err, sizeof(err), "failed to create sandbox");
        json_reply(req, 500, err);
        return KORE_RESULT_OK;
    }

    if (root) cJSON_Delete(root);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "id", id);
    char *json_str = cJSON_PrintUnformatted(resp);
    json_reply(req, 201, json_str);
    free(json_str);
    cJSON_Delete(resp);

    return KORE_RESULT_OK;
}

/* ------------------------------------------------------------------
 * Route: POST /api/sandbox/:id/exec
 *
 * Body (JSON):
 *   { "path": "/bin/echo", "args": ["hello", "world"] }
 *
 * Response 200:
 *   { "exit_code": 0, "output": "hello world\n" }
 * ------------------------------------------------------------------ */
int route_sandbox_exec(struct http_request *req)
{
    if (req->method != HTTP_METHOD_POST) {
        json_reply(req, 405, "{\"error\":\"method not allowed\"}");
        return KORE_RESULT_OK;
    }

    const char *id = http_argument_get_string(req, "id", NULL);
    if (!id) {
        json_reply(req, 400, "{\"error\":\"missing sandbox id\"}");
        return KORE_RESULT_OK;
    }

    /* Parse request body */
    struct kore_buf *buf = kore_buf_alloc(req->http_body->length + 1);
    kore_buf_append(buf, req->http_body->data, req->http_body->length);
    kore_buf_append(buf, "\0", 1);
    
    cJSON *root = cJSON_Parse((const char *)buf->data);
    kore_buf_free(buf);

    if (!root) {
        json_reply(req, 400, "{\"error\":\"invalid json body\"}");
        return KORE_RESULT_OK;
    }

    cJSON *path_obj = cJSON_GetObjectItem(root, "path");
    if (!cJSON_IsString(path_obj)) {
        cJSON_Delete(root);
        json_reply(req, 400, "{\"error\":\"missing or invalid 'path'\"}");
        return KORE_RESULT_OK;
    }
    const char *path = path_obj->valuestring;

    cJSON *args_obj = cJSON_GetObjectItem(root, "args");
    int argc = 0;
    if (cJSON_IsArray(args_obj)) argc = cJSON_GetArraySize(args_obj);

    char **argv = kore_calloc(argc + 2, sizeof(char *));
    argv[0] = kore_strdup(path);
    for (int i = 0; i < argc; i++) {
        cJSON *arg = cJSON_GetArrayItem(args_obj, i);
        argv[i+1] = kore_strdup(cJSON_IsString(arg) ? arg->valuestring : "");
    }
    argv[argc+1] = NULL;

    char out_buf[SANDBOX_OUT_MAX];
    int  exit_code = -1;

    if (sandbox_exec(id, path, argv, out_buf, sizeof(out_buf), &exit_code) != 0) {
        for (int i = 0; i <= argc; i++) kore_free(argv[i]);
        kore_free(argv);
        cJSON_Delete(root);
        char err[128];
        snprintf(err, sizeof(err), "{\"error\":\"exec failed: %s\"}", strerror(errno));
        json_reply(req, 500, err);
        return KORE_RESULT_OK;
    }

    for (int i = 0; i <= argc; i++) kore_free(argv[i]);
    kore_free(argv);
    cJSON_Delete(root);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddNumberToObject(resp, "exit_code", exit_code);
    cJSON_AddStringToObject(resp, "output", out_buf);
    char *json_str = cJSON_PrintUnformatted(resp);
    json_reply(req, 200, json_str);
    free(json_str);
    cJSON_Delete(resp);

    return KORE_RESULT_OK;
}

/* ------------------------------------------------------------------
 * Route: DELETE /api/sandbox/:id
 * ------------------------------------------------------------------ */
int route_sandbox_destroy(struct http_request *req)
{
    if (req->method != HTTP_METHOD_DELETE) {
        json_reply(req, 405, "{\"error\":\"method not allowed\"}");
        return KORE_RESULT_OK;
    }

    const char *id = http_argument_get_string(req, "id", NULL);
    if (!id) {
        json_reply(req, 400, "{\"error\":\"missing sandbox id\"}");
        return KORE_RESULT_OK;
    }

    if (sandbox_destroy(id) != 0) {
        json_reply(req, 404, "{\"error\":\"sandbox not found\"}");
        return KORE_RESULT_OK;
    }

    json_reply(req, 200, "{\"status\":\"destroyed\"}");
    return KORE_RESULT_OK;
}

/* ------------------------------------------------------------------
 * Route: POST /api/jobs
 *
 * Body (JSON):
 *   { "type": "sandbox.exec", "payload": "{...}", "delay_sec": 0 }
 *
 * Response 201:
 *   { "id": "<uuid>" }
 * ------------------------------------------------------------------ */
int route_job_enqueue(struct http_request *req)
{
    if (req->method != HTTP_METHOD_POST) {
        json_reply(req, 405, "{\"error\":\"method not allowed\"}");
        return KORE_RESULT_OK;
    }

    /*
     * TODO: parse type, payload, delay_sec from JSON body.
     * Illustration with hardcoded values:
     */
    const char *type    = "sandbox.exec";
    const char *payload = "{\"sandbox_id\":\"demo\",\"cmd\":\"/bin/echo\"}";
    int         delay   = 0;

    char job_id[WORKFLOW_JOB_ID_LEN];
    if (workflow_enqueue(type, payload, delay, job_id) != 0) {
        json_reply(req, 500, "{\"error\":\"enqueue failed\"}");
        return KORE_RESULT_OK;
    }

    char body[64];
    snprintf(body, sizeof(body), "{\"id\":\"%s\"}", job_id);
    json_reply(req, 201, body);
    return KORE_RESULT_OK;
}

/* ------------------------------------------------------------------
 * Route: GET /api/jobs/:id
 * ------------------------------------------------------------------ */
int route_job_get(struct http_request *req)
{
    if (req->method != HTTP_METHOD_GET) {
        json_reply(req, 405, "{\"error\":\"method not allowed\"}");
        return KORE_RESULT_OK;
    }

    const char *id = http_argument_get_string(req, "id", NULL);
    if (!id) {
        json_reply(req, 400, "{\"error\":\"missing job id\"}");
        return KORE_RESULT_OK;
    }

    job_t job;
    memset(&job, 0, sizeof(job));
    if (workflow_get_job(id, &job) != 0) {
        json_reply(req, 404, "{\"error\":\"job not found\"}");
        return KORE_RESULT_OK;
    }

    const char *status_str[] = { "pending", "running", "done", "failed" };
    char body[256];
    snprintf(body, sizeof(body),
        "{\"id\":\"%s\",\"type\":\"%s\",\"status\":\"%s\",\"retry_count\":%d}",
        job.id, job.type,
        status_str[job.status],
        job.retry_count);

    free(job.payload);
    json_reply(req, 200, body);
    return KORE_RESULT_OK;
}

/* ------------------------------------------------------------------
 * Route: DELETE /api/jobs/:id  (cancel)
 * ------------------------------------------------------------------ */
int route_job_cancel(struct http_request *req)
{
    if (req->method != HTTP_METHOD_DELETE) {
        json_reply(req, 405, "{\"error\":\"method not allowed\"}");
        return KORE_RESULT_OK;
    }

    const char *id = http_argument_get_string(req, "id", NULL);
    if (!id) {
        json_reply(req, 400, "{\"error\":\"missing job id\"}");
        return KORE_RESULT_OK;
    }

    if (workflow_cancel_job(id) != 0) {
        json_reply(req, 404, "{\"error\":\"job not found or not cancellable\"}");
        return KORE_RESULT_OK;
    }

    json_reply(req, 200, "{\"status\":\"cancelled\"}");
    return KORE_RESULT_OK;
}

/* ------------------------------------------------------------------
 * Route: POST /api/agent/task
 *
 * Body (JSON):
 *   { "goal": "Research the latest news on AI and summarize it." }
 *
 * Response 201:
 *   { "id": "<uuid>" }
 * ------------------------------------------------------------------ */
int route_agent_task_create(struct http_request *req)
{
    if (req->method != HTTP_METHOD_POST) {
        json_reply(req, 405, "{\"error\":\"method not allowed\"}");
        return KORE_RESULT_OK;
    }

    /* Parse request body */
    struct kore_buf *buf = kore_buf_alloc(req->http_body->length + 1);
    kore_buf_append(buf, req->http_body->data, req->http_body->length);
    kore_buf_append(buf, "\0", 1);
    
    cJSON *root = cJSON_Parse((const char *)buf->data);
    kore_buf_free(buf);

    if (!root) {
        json_reply(req, 400, "{\"error\":\"invalid json body\"}");
        return KORE_RESULT_OK;
    }

    cJSON *goal_obj = cJSON_GetObjectItem(root, "goal");
    if (!cJSON_IsString(goal_obj)) {
        cJSON_Delete(root);
        json_reply(req, 400, "{\"error\":\"missing or invalid 'goal'\"}");
        return KORE_RESULT_OK;
    }

    agent_task_t *task = agent_task_create(goal_obj->valuestring);
    cJSON_Delete(root);

    if (!task) {
        json_reply(req, 500, "{\"error\":\"failed to create agent task\"}");
        return KORE_RESULT_OK;
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddStringToObject(resp, "id", task->id);
    char *json_str = cJSON_PrintUnformatted(resp);
    json_reply(req, 201, json_str);
    free(json_str);
    cJSON_Delete(resp);

    return KORE_RESULT_OK;
}
