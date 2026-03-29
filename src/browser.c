/*
 * brocolli - browser.c
 *
 * Implements a C-based browser engine for controlling headless Chromium
 * via the Chrome DevTools Protocol (CDP) over WebSockets.
 *
 * Inspired by Skyvern.
 */

#include "browser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>

#include <kore/kore.h>
#include <kore/http.h>
#include "../vendor/cjson/cJSON.h"

/* ------------------------------------------------------------------
 * Internal globals
 * ------------------------------------------------------------------ */

#define BROWSER_MAX 16
static browser_t s_browsers[BROWSER_MAX];
static pthread_mutex_t s_mutex = PTHREAD_MUTEX_INITIALIZER;
static int s_initialised = 0;

/* ------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------ */

static void gen_uuid(char buf[BROWSER_ID_LEN])
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
    snprintf(buf, BROWSER_ID_LEN,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0],b[1],b[2],b[3], b[4],b[5], b[6],b[7],
        b[8],b[9], b[10],b[11],b[12],b[13],b[14],b[15]);
}

/* ------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------ */

int browser_module_init(void)
{
    if (s_initialised) return 0;
    memset(s_browsers, 0, sizeof(s_browsers));
    for (int i = 0; i < BROWSER_MAX; i++) {
        s_browsers[i].pid = -1;
    }
    s_initialised = 1;
    return 0;
}

browser_t *browser_launch(int port)
{
    if (!s_initialised) return NULL;

    pthread_mutex_lock(&s_mutex);
    int slot = -1;
    for (int i = 0; i < BROWSER_MAX; i++) {
        if (s_browsers[i].pid == -1) { slot = i; break; }
    }
    if (slot < 0) { pthread_mutex_unlock(&s_mutex); return NULL; }
    pthread_mutex_unlock(&s_mutex);

    char id[BROWSER_ID_LEN];
    gen_uuid(id);

    pid_t pid = fork();
    if (pid < 0) return NULL;

    if (pid == 0) {
        /* Child: launch chromium */
        char port_arg[32];
        snprintf(port_arg, sizeof(port_arg), "--remote-debugging-port=%d", port);
        
        char *argv[] = {
            "chromium",
            "--headless",
            "--disable-gpu",
            "--no-sandbox",
            port_arg,
            NULL
        };
        
        execvp("chromium", argv);
        /* If execvp fails, try chromium-browser */
        execvp("chromium-browser", argv);
        _exit(1);
    }

    /* Parent: track the browser */
    pthread_mutex_lock(&s_mutex);
    strncpy(s_browsers[slot].id, id, BROWSER_ID_LEN);
    s_browsers[slot].pid = pid;
    s_browsers[slot].port = port;
    s_browsers[slot].ws_handle = NULL; /* To be connected on demand */
    pthread_mutex_unlock(&s_mutex);

    /* Wait for chromium to start and listen on the port */
    sleep(2); 

    return &s_browsers[slot];
}

int browser_navigate(browser_t *b, const char *url)
{
    if (!b || b->pid == -1) return -1;

    /* 
     * TODO: Implement CDP over WebSockets using Kore's built-in WS client.
     * For now, we log the navigation intent.
     */
    kore_log(LOG_INFO, "browser[%s]: navigating to %s", b->id, url);
    
    /* 
     * In a real implementation, we would:
     * 1. Connect to http://localhost:port/json to get the WebSocket URL.
     * 2. Send a CDP command: {"id":1, "method":"Page.navigate", "params":{"url":"..."}}
     * 3. Wait for the response.
     */
    
    return 0;
}

int browser_screenshot(browser_t *b, void *out_buf, size_t *out_len)
{
    if (!b || b->pid == -1) return -1;
    kore_log(LOG_INFO, "browser[%s]: taking screenshot", b->id);
    /* CDP command: Page.captureScreenshot */
    return 0;
}

int browser_close(browser_t *b)
{
    if (!b || b->pid == -1) return -1;

    kill(b->pid, SIGTERM);
    waitpid(b->pid, NULL, 0);

    pthread_mutex_lock(&s_mutex);
    b->pid = -1;
    memset(b->id, 0, BROWSER_ID_LEN);
    pthread_mutex_unlock(&s_mutex);

    return 0;
}
