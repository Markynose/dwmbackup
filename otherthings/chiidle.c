/* chiidle.c — X11 idle daemon; locks screen after timeout */
/* usage: chiidle [-t minutes] &   (default: 5 minutes) */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>

#define DEFAULT_TIMEOUT_MIN 5
#define POLL_SECS           10

static volatile int running = 1;
static void sighand(int s) { (void)s; running = 0; }

int main(int argc, char *argv[]) {
	int timeout_min = DEFAULT_TIMEOUT_MIN;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-t") && i + 1 < argc)
			timeout_min = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-h")) {
			printf("usage: chiidle [-t minutes]\n");
			return 0;
		}
	}

	unsigned long timeout_ms = (unsigned long)timeout_min * 60 * 1000;

	Display *dpy = XOpenDisplay(NULL);
	if (!dpy) { fprintf(stderr, "chiidle: cannot open display\n"); return 1; }

	int ev, err;
	if (!XScreenSaverQueryExtension(dpy, &ev, &err)) {
		fprintf(stderr, "chiidle: XScreenSaver extension not available\n");
		XCloseDisplay(dpy);
		return 1;
	}

	signal(SIGTERM, sighand);
	signal(SIGINT,  sighand);

	XScreenSaverInfo *info = XScreenSaverAllocInfo();
	int locked = 0; /* track so we don't lock repeatedly */

	while (running) {
		XScreenSaverQueryInfo(dpy, DefaultRootWindow(dpy), info);
		unsigned long idle = info->idle; /* milliseconds */

		if (!locked && idle >= timeout_ms) {
			/* spawn lock command — pass XAUTHORITY via env */
			const char *xauth = getenv("XAUTHORITY");
			char cmd[512];
			if (xauth && *xauth)
				snprintf(cmd, sizeof(cmd),
				         "doas env XAUTHORITY=\"%s\" chilock", xauth);
			else
				snprintf(cmd, sizeof(cmd), "doas chilock");
			system(cmd);
			locked = 1;
		} else if (locked && idle < (unsigned long)(POLL_SECS * 1000)) {
			/* user became active again after lock */
			locked = 0;
		}

		sleep(POLL_SECS);
	}

	XFree(info);
	XCloseDisplay(dpy);
	return 0;
}
