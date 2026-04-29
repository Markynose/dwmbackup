/* chilock.c — X11 screen locker, pink theme; run as: doas chilock */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>
#include <shadow.h>
#include <crypt.h>
#include <sys/select.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xft/Xft.h>

#define C_BG    "#1e1224"
#define C_MID   "#3d2b45"
#define C_FAIL  "#c9479b"
#define C_FG    "#f0b8d0"
#define C_DIM   "#8b6e7e"
#define FBIG    "JetBrainsMono Nerd Font:size=60:bold"
#define FSM     "JetBrainsMono Nerd Font:size=13"
#define MAX_PW  256

typedef enum { ST_INIT, ST_INPUT, ST_FAIL } State;

static Display  *dpy;
static int       scr;
static Window    root, win;
static Visual   *vis;
static Colormap  cmap;
static XftDraw  *xd;
static XftFont  *fbig, *fsm;
static XftColor  cfg, cdim, cfail;
static int       sw, sh;

static unsigned long xcolor(const char *name) {
	XColor c, dummy;
	XAllocNamedColor(dpy, cmap, name, &c, &dummy);
	return c.pixel;
}

static void xftcolor(const char *name, XftColor *out) {
	XftColorAllocName(dpy, vis, cmap, name, out);
}

static void drawstr(XftFont *f, XftColor *c, int y, const char *s) {
	XGlyphInfo ext;
	XftTextExtentsUtf8(dpy, f, (const FcChar8 *)s, strlen(s), &ext);
	XftDrawStringUtf8(xd, c, f, (sw - ext.width) / 2, y,
	                  (const FcChar8 *)s, strlen(s));
}

static void redraw(State st, int npw) {
	unsigned long bg = xcolor(st == ST_FAIL  ? C_FAIL :
	                           st == ST_INPUT ? C_MID  : C_BG);
	XSetWindowBackground(dpy, win, bg);
	XClearWindow(dpy, win);

	/* Clock */
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	char tbuf[16], dbuf[32];
	strftime(tbuf, sizeof(tbuf), "%H:%M:%S", tm);
	strftime(dbuf, sizeof(dbuf), "%A, %B %d", tm);
	drawstr(fbig, &cfg,  sh / 2 - 30, tbuf);
	drawstr(fsm,  &cdim, sh / 2 + 30, dbuf);

	/* Password / status */
	char status[MAX_PW + 32];
	if (st == ST_FAIL) {
		snprintf(status, sizeof(status), "incorrect password");
	} else if (npw > 0) {
		memset(status, '*', npw);
		status[npw] = '\0';
	} else {
		snprintf(status, sizeof(status), "chilock  enter password");
	}
	drawstr(fsm, st == ST_FAIL ? &cfail : &cdim, sh / 2 + 60, status);

	XFlush(dpy);
}

static int check_pw(const char *user, const char *pw) {
	struct spwd *sp = getspnam(user);
	if (!sp) {
		/* shadow read failed — try pw_passwd (works if no shadow) */
		struct passwd *p = getpwnam(user);
		if (!p || !p->pw_passwd || p->pw_passwd[0] == 'x') return 0;
		char *h = crypt(pw, p->pw_passwd);
		return h && strcmp(h, p->pw_passwd) == 0;
	}
	char *h = crypt(pw, sp->sp_pwdp);
	return h && strcmp(h, sp->sp_pwdp) == 0;
}

int main(void) {
	/* Determine the user we're locking for */
	const char *user = getenv("DOAS_USER");
	if (!user || !*user) user = getenv("SUDO_USER");
	if (!user || !*user) user = getlogin();
	if (!user || !*user) user = getenv("LOGNAME");
	if (!user || !*user) user = getenv("USER");
	if (!user || !*user) {
		/* last resort: real uid (only meaningful if not root) */
		struct passwd *pw = getpwuid(getuid());
		if (pw) user = pw->pw_name;
	}
	if (!user || !*user) {
		fprintf(stderr, "chilock: cannot determine user — run: doas chilock\n");
		return 1;
	}

	/* doas strips XAUTHORITY — restore it from the user's home dir */
	if (!getenv("XAUTHORITY")) {
		struct passwd *upw = getpwnam(user);
		if (upw) {
			char xauth[512];
			snprintf(xauth, sizeof(xauth), "%s/.Xauthority", upw->pw_dir);
			setenv("XAUTHORITY", xauth, 0);
		}
	}

	const char *disp = getenv("DISPLAY");
	if (!disp || !*disp) disp = ":0";
	dpy = XOpenDisplay(disp);
	if (!dpy) { fprintf(stderr, "chilock: cannot open display %s\n", disp); return 1; }

	scr  = DefaultScreen(dpy);
	root = DefaultRootWindow(dpy);
	vis  = DefaultVisual(dpy, scr);
	cmap = DefaultColormap(dpy, scr);
	sw   = DisplayWidth(dpy, scr);
	sh   = DisplayHeight(dpy, scr);

	/* Fullscreen override-redirect window */
	XSetWindowAttributes wa = {0};
	wa.override_redirect = True;
	wa.background_pixel  = xcolor(C_BG);
	wa.event_mask        = KeyPressMask | ExposureMask;
	win = XCreateWindow(dpy, root, 0, 0, sw, sh, 0,
	                    DefaultDepth(dpy, scr), InputOutput, vis,
	                    CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);
	XMapRaised(dpy, win);
	XFlush(dpy);

	/* Xft setup */
	xd   = XftDrawCreate(dpy, win, vis, cmap);
	fbig = XftFontOpenName(dpy, scr, FBIG);
	fsm  = XftFontOpenName(dpy, scr, FSM);
	if (!fbig) fbig = XftFontOpenName(dpy, scr, "monospace:size=60:bold");
	if (!fsm)  fsm  = XftFontOpenName(dpy, scr, "monospace:size=13");
	xftcolor(C_FG,   &cfg);
	xftcolor(C_DIM,  &cdim);
	xftcolor(C_FAIL, &cfail);

	/* Grab input — retry a few times in case another grab is active */
	int grabbed = 0;
	for (int i = 0; i < 10 && !grabbed; i++) {
		int kg = XGrabKeyboard(dpy, root, True,
		                       GrabModeAsync, GrabModeAsync, CurrentTime);
		int pg = XGrabPointer(dpy, root, False, ButtonPressMask,
		                      GrabModeAsync, GrabModeAsync,
		                      None, None, CurrentTime);
		if (kg == GrabSuccess && pg == GrabSuccess) {
			grabbed = 1;
		} else {
			XUngrabKeyboard(dpy, CurrentTime);
			XUngrabPointer(dpy, CurrentTime);
			usleep(100000);
		}
	}
	if (!grabbed) {
		fprintf(stderr, "chilock: cannot grab keyboard/pointer\n");
		return 1;
	}

	char pw[MAX_PW + 1] = {0};
	int  npw = 0;
	State st = ST_INIT;
	int  xfd = ConnectionNumber(dpy);

	redraw(st, 0);

	while (1) {
		/* 1-second timeout for clock updates */
		if (!XPending(dpy)) {
			fd_set fds;
			struct timeval tv = {1, 0};
			FD_ZERO(&fds);
			FD_SET(xfd, &fds);
			select(xfd + 1, &fds, NULL, NULL, &tv);
		}
		if (!XPending(dpy)) {
			redraw(st, npw);
			continue;
		}

		XEvent ev;
		XNextEvent(dpy, &ev);

		if (ev.type == Expose) { redraw(st, npw); continue; }
		if (ev.type != KeyPress) continue;

		char buf[32] = {0};
		KeySym ks;
		XLookupString(&ev.xkey, buf, sizeof(buf) - 1, &ks, NULL);

		if (ks == XK_Return || ks == XK_KP_Enter) {
			pw[npw] = '\0';
			if (check_pw(user, pw)) break; /* unlocked */
			st = ST_FAIL;
			memset(pw, 0, sizeof(pw));
			npw = 0;
			redraw(st, 0);
			usleep(500000); /* brief flash */
			st = ST_INIT;
			redraw(st, 0);
		} else if (ks == XK_BackSpace || ks == XK_Delete) {
			if (npw > 0) {
				pw[--npw] = '\0';
				st = npw > 0 ? ST_INPUT : ST_INIT;
				redraw(st, npw);
			}
		} else if (ks == XK_Escape) {
			memset(pw, 0, sizeof(pw));
			npw = 0;
			st = ST_INIT;
			redraw(st, 0);
		} else if (buf[0] >= ' ' && buf[0] < 127 && npw < MAX_PW) {
			pw[npw++] = buf[0];
			st = ST_INPUT;
			redraw(st, npw);
		}
	}

	memset(pw, 0, sizeof(pw));
	XUngrabKeyboard(dpy, CurrentTime);
	XUngrabPointer(dpy, CurrentTime);
	XftDrawDestroy(xd);
	XftFontClose(dpy, fbig);
	XftFontClose(dpy, fsm);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}
