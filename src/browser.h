#pragma once

/*
 * brocolli - browser engine
 *
 * Provides a C interface to control headless Chromium via the
 * Chrome DevTools Protocol (CDP) over WebSockets.
 *
 * Inspired by Skyvern.
 */

#include <stddef.h>

/* ------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------ */

#define BROWSER_ID_LEN      37
#define BROWSER_URL_MAX     2048
#define BROWSER_PORT_DEFAULT 9222

/* ------------------------------------------------------------------
 * Types
 * ------------------------------------------------------------------ */

typedef struct {
    char    id[BROWSER_ID_LEN];
    int     port;
    pid_t   pid;
    void    *ws_handle; /* Internal WebSocket handle */
} browser_t;

/* ------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------ */

/*
 * browser_module_init()
 *   Initialise the browser engine.
 */
int  browser_module_init(void);

/*
 * browser_launch()
 *   Start a new headless Chromium instance.
 *   Returns a pointer to the browser handle or NULL on error.
 */
browser_t *browser_launch(int port);

/*
 * browser_navigate()
 *   Navigate the browser to a specific URL.
 */
int  browser_navigate(browser_t *b, const char *url);

/*
 * browser_screenshot()
 *   Capture a screenshot of the current page.
 *   Writes PNG data into out_buf.
 */
int  browser_screenshot(browser_t *b, void *out_buf, size_t *out_len);

/*
 * browser_close()
 *   Terminate the Chromium process and free resources.
 */
int  browser_close(browser_t *b);
