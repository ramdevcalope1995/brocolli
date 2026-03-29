# Browser Skill

This skill provides a C interface to control headless Chromium via the Chrome DevTools Protocol (CDP).

## Tools

### browser_launch
Start a new headless Chromium instance.
- `port`: (number) The remote debugging port to use (default: 9222).

### browser_navigate
Navigate the browser to a specific URL.
- `id`: (string) The UUID of the browser instance.
- `url`: (string) The URL to navigate to.

### browser_screenshot
Capture a screenshot of the current page.
- `id`: (string) The UUID of the browser instance.

### browser_close
Terminate the Chromium process and free resources.
- `id`: (string) The UUID of the browser instance to close.
