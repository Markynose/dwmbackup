#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

static Display *dpy;

static char *get_str_prop(Window w, Atom atom) {
	Atom type; int fmt; unsigned long n, extra;
	unsigned char *data = NULL;
	if (XGetWindowProperty(dpy, w, atom, 0, 256, False,
	    AnyPropertyType, &type, &fmt, &n, &extra, &data) == Success && data) {
		char *s = strdup((char *)data);
		XFree(data);
		return s;
	}
	return NULL;
}

static long get_long_prop(Window w, Atom atom) {
	Atom type; int fmt; unsigned long n, extra;
	unsigned char *data = NULL;
	long val = -1;
	if (XGetWindowProperty(dpy, w, atom, 0, 1, False,
	    XA_CARDINAL, &type, &fmt, &n, &extra, &data) == Success && data) {
		val = *(long *)data;
		XFree(data);
	}
	return val;
}

static void focus_window(Window w) {
	Atom net_active = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
	XEvent ev = {0};
	ev.xclient.type         = ClientMessage;
	ev.xclient.window       = w;
	ev.xclient.message_type = net_active;
	ev.xclient.format       = 32;
	ev.xclient.data.l[0]    = 2; /* pager */
	ev.xclient.data.l[1]    = CurrentTime;
	XSendEvent(dpy, DefaultRootWindow(dpy), False,
	    SubstructureRedirectMask | SubstructureNotifyMask, &ev);
	XFlush(dpy);
}

int main(void) {
	dpy = XOpenDisplay(NULL);
	if (!dpy) { fprintf(stderr, "chioverview: cannot open display\n"); return 1; }

	Window root = DefaultRootWindow(dpy);
	Atom net_client_list = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
	Atom net_wm_name     = XInternAtom(dpy, "_NET_WM_NAME",     False);
	Atom net_wm_desktop  = XInternAtom(dpy, "_NET_WM_DESKTOP",  False);

	/* get client list */
	Atom type; int fmt; unsigned long n, extra;
	unsigned char *data = NULL;
	if (XGetWindowProperty(dpy, root, net_client_list, 0, 1024, False,
	    XA_WINDOW, &type, &fmt, &n, &extra, &data) != Success || !data) {
		fprintf(stderr, "chioverview: no windows\n");
		return 1;
	}
	Window *wins = (Window *)data;

	/* build lines: "wid tag: title" */
	char **lines = malloc(n * sizeof(char *));
	Window *wids = malloc(n * sizeof(Window));
	int count = 0;

	for (unsigned long i = 0; i < n; i++) {
		char *name = get_str_prop(wins[i], net_wm_name);
		if (!name) name = get_str_prop(wins[i], XA_WM_NAME);
		if (!name) name = strdup("(unnamed)");

		long tag = get_long_prop(wins[i], net_wm_desktop);
		char line[512];
		if (tag >= 0)
			snprintf(line, sizeof(line), "[%ld] %s", tag + 1, name);
		else
			snprintf(line, sizeof(line), "[?] %s", name);

		lines[count] = strdup(line);
		wids[count]  = wins[i];
		count++;
		free(name);
	}
	XFree(data);

	if (count == 0) { fprintf(stderr, "chioverview: no windows\n"); return 1; }

	/* pipe lines to dmenu, read selection back */
	int in_fd[2], out_fd[2];
	pipe(in_fd); pipe(out_fd);

	pid_t pid = fork();
	if (pid == 0) {
		dup2(in_fd[0],  STDIN_FILENO);
		dup2(out_fd[1], STDOUT_FILENO);
		close(in_fd[0]); close(in_fd[1]);
		close(out_fd[0]); close(out_fd[1]);
		execlp("dmenu", "dmenu", "-l", "20", "-p", "window:",
		    "-nb", "#1e1224", "-nf", "#f0b8d0",
		    "-sb", "#c9479b", "-sf", "#ffffff", NULL);
		_exit(1);
	}
	close(in_fd[0]); close(out_fd[1]);

	/* write lines to dmenu stdin */
	FILE *fin = fdopen(in_fd[1], "w");
	for (int i = 0; i < count; i++) fprintf(fin, "%s\n", lines[i]);
	fclose(fin);

	/* read selection from dmenu stdout */
	char chosen[512] = {0};
	FILE *fout = fdopen(out_fd[0], "r");
	if (!fgets(chosen, sizeof(chosen), fout)) { fclose(fout); return 0; }
	fclose(fout);
	waitpid(pid, NULL, 0);

	chosen[strcspn(chosen, "\n")] = 0;
	if (!chosen[0]) return 0;

	/* match chosen line to window */
	for (int i = 0; i < count; i++) {
		if (!strcmp(lines[i], chosen)) {
			focus_window(wids[i]);
			break;
		}
	}

	for (int i = 0; i < count; i++) free(lines[i]);
	free(lines); free(wids);
	XCloseDisplay(dpy);
	return 0;
}
