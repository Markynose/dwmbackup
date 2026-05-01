/*
 * chibar — status bar for sxwm
 *
 * reads: _NET_CURRENT_DESKTOP, _NET_CLIENT_LIST, _NET_ACTIVE_WINDOW,
 *        _NET_WM_NAME, WM_NAME (root = dwmstatus output)
 * sets:  _NET_WM_WINDOW_TYPE_DOCK + _NET_WM_STRUT_PARTIAL so sxwm
 *        leaves space at the top
 *
 * build: cc chibar.c -o chibar -lX11 -lXft
 */

#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xft/Xft.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- tunables --- */
#define BAR_H   22
#define PAD      8
#define FONT    "JetBrainsMono Nerd Font:size=10"
#define NDESKS   9

/* pink theme */
#define S_BG     "#1e1224"
#define S_FG     "#f0b8d0"
#define S_ACCENT "#c9479b"
#define S_DIM    "#3d2b45"
#define S_WHITE  "#ffffff"

static Display  *dpy;
static Window    root, bar;
static int       scr, sw;
static Visual   *vis;
static Colormap  cmap;
static GC        gc;
static XftFont  *font;
static XftDraw  *xd;
static XftColor  col_bg, col_fg, col_accent, col_dim, col_white;

static Atom net_cur_desk, net_num_desks, net_active, net_wm_name,
            net_wm_type, net_wm_type_dock, net_strut, net_strut_partial,
            net_client_list, net_wm_desktop, utf8_str;

static char status[512] = {0};
static char title[256]  = {0};
static int  cur_desk     = 0;
static int  num_desks    = NDESKS;
static int  occupied[NDESKS] = {0};

/* --- helpers --- */

static unsigned char *get_prop(Window w, Atom prop, Atom type, unsigned long *n) {
	Atom actual; int fmt; unsigned long bytes;
	unsigned char *data = NULL;
	*n = 0;
	if (XGetWindowProperty(dpy, w, prop, 0, 1024, False, type,
	                       &actual, &fmt, n, &bytes, &data) != Success)
		return NULL;
	return data;
}

static void update_status(void) {
	unsigned long n;
	unsigned char *s = get_prop(root, XA_WM_NAME, XA_STRING, &n);
	if (!s) s = get_prop(root, net_wm_name, utf8_str, &n);
	if (s) { strncpy(status, (char*)s, sizeof(status)-1); XFree(s); }
}

static void update_desktop(void) {
	unsigned long n;
	long *d = (long*)get_prop(root, net_cur_desk, XA_CARDINAL, &n);
	if (d) { cur_desk = (int)*d; XFree(d); }
	long *nd = (long*)get_prop(root, net_num_desks, XA_CARDINAL, &n);
	if (nd) { num_desks = (int)*nd; if (num_desks > NDESKS) num_desks = NDESKS; XFree(nd); }
}

static void update_occupied(void) {
	memset(occupied, 0, sizeof(occupied));
	unsigned long n;
	Window *clients = (Window*)get_prop(root, net_client_list, XA_WINDOW, &n);
	if (!clients) return;
	for (unsigned long i = 0; i < n; i++) {
		unsigned long dn;
		long *d = (long*)get_prop(clients[i], net_wm_desktop, XA_CARDINAL, &dn);
		if (d) { int k = (int)*d; if (k >= 0 && k < NDESKS) occupied[k] = 1; XFree(d); }
	}
	XFree(clients);
}

static void update_title(void) {
	title[0] = 0;
	unsigned long n;
	Window *aw = (Window*)get_prop(root, net_active, XA_WINDOW, &n);
	if (!aw || !*aw) { if (aw) XFree(aw); return; }
	Window w = *aw; XFree(aw);
	unsigned char *t = get_prop(w, net_wm_name, utf8_str, &n);
	if (!t) t = get_prop(w, XA_WM_NAME, XA_STRING, &n);
	if (t) { strncpy(title, (char*)t, sizeof(title)-1); XFree(t); }
	XSelectInput(dpy, w, PropertyChangeMask);
}

/* --- drawing --- */

static int txtw(const char *s) {
	XGlyphInfo ext;
	XftTextExtentsUtf8(dpy, font, (FcChar8*)s, strlen(s), &ext);
	return ext.xOff;
}

static void drect(int x, int y, int w, int h, XftColor *c) {
	XSetForeground(dpy, gc, c->pixel);
	XFillRectangle(dpy, bar, gc, x, y, w, h);
}

static void dtext(int x, int y, const char *s, XftColor *c) {
	XftDrawStringUtf8(xd, c, font, x, y, (FcChar8*)s, strlen(s));
}

static void draw(void) {
	int ty = (BAR_H + font->ascent - font->descent) / 2;

	/* background */
	drect(0, 0, sw, BAR_H, &col_bg);

	/* --- left: workspaces --- */
	int x = PAD / 2;
	for (int i = 0; i < num_desks; i++) {
		char ws[3];
		snprintf(ws, sizeof(ws), "%d", i + 1);
		int w = txtw(ws) + PAD;

		if (i == cur_desk) {
			drect(x, 0, w + PAD/2, BAR_H, &col_accent);
			dtext(x + PAD/2, ty, ws, &col_white);
		} else if (occupied[i]) {
			dtext(x + PAD/2, ty, ws, &col_fg);
		} else {
			dtext(x + PAD/2, ty, ws, &col_dim);
		}
		x += w + PAD/2;
	}
	int left_end = x + PAD;

	/* --- right: status --- */
	int right_start = sw - txtw(status) - PAD;
	if (status[0]) dtext(right_start, ty, status, &col_fg);

	/* --- center: window title --- */
	if (title[0]) {
		int tw = txtw(title);
		int tx = (sw - tw) / 2;
		if (tx < left_end) tx = left_end;
		/* clip title if it would overlap status */
		if (tx + tw > right_start - PAD) {
			/* truncate with ellipsis */
			char trunc[256];
			strncpy(trunc, title, sizeof(trunc)-4);
			int len = strlen(trunc);
			while (len > 0 && tx + txtw(trunc) > right_start - PAD - txtw("...")) {
				trunc[--len] = 0;
			}
			strncat(trunc, "...", 3);
			dtext(tx, ty, trunc, &col_fg);
		} else {
			dtext(tx, ty, title, &col_fg);
		}
	}

	/* separator line at bottom */
	drect(0, BAR_H - 1, sw, 1, &col_accent);

	XFlush(dpy);
}

/* --- setup --- */

static void setup_strut(void) {
	long strut[12] = {0};
	strut[2] = BAR_H;       /* top */
	strut[8] = 0;            /* top_start_x */
	strut[9] = sw - 1;       /* top_end_x */
	XChangeProperty(dpy, bar, net_strut_partial, XA_CARDINAL, 32,
	                PropModeReplace, (unsigned char*)strut, 12);
	long s4[4] = {0, 0, BAR_H, 0};
	XChangeProperty(dpy, bar, net_strut, XA_CARDINAL, 32,
	                PropModeReplace, (unsigned char*)s4, 4);
}

int main(void) {
	dpy = XOpenDisplay(NULL);
	if (!dpy) { fprintf(stderr, "chibar: cannot open display\n"); return 1; }

	scr  = DefaultScreen(dpy);
	root = RootWindow(dpy, scr);
	sw   = DisplayWidth(dpy, scr);
	vis  = DefaultVisual(dpy, scr);
	cmap = DefaultColormap(dpy, scr);

	/* intern atoms */
	net_cur_desk        = XInternAtom(dpy, "_NET_CURRENT_DESKTOP",     False);
	net_num_desks       = XInternAtom(dpy, "_NET_NUMBER_OF_DESKTOPS",  False);
	net_active          = XInternAtom(dpy, "_NET_ACTIVE_WINDOW",       False);
	net_wm_name         = XInternAtom(dpy, "_NET_WM_NAME",             False);
	net_wm_type         = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE",      False);
	net_wm_type_dock    = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
	net_strut           = XInternAtom(dpy, "_NET_WM_STRUT",            False);
	net_strut_partial   = XInternAtom(dpy, "_NET_WM_STRUT_PARTIAL",    False);
	net_client_list     = XInternAtom(dpy, "_NET_CLIENT_LIST",         False);
	net_wm_desktop      = XInternAtom(dpy, "_NET_WM_DESKTOP",          False);
	utf8_str            = XInternAtom(dpy, "UTF8_STRING",              False);

	/* bar window */
	XSetWindowAttributes wa;
	wa.override_redirect = True;
	wa.background_pixel  = BlackPixel(dpy, scr);
	wa.event_mask        = ExposureMask;
	bar = XCreateWindow(dpy, root, 0, 0, sw, BAR_H, 0,
	                    DefaultDepth(dpy, scr), InputOutput, vis,
	                    CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);

	XChangeProperty(dpy, bar, net_wm_type, XA_ATOM, 32,
	                PropModeReplace, (unsigned char*)&net_wm_type_dock, 1);
	setup_strut();

	/* font + colors */
	font = XftFontOpenName(dpy, scr, FONT);
	if (!font) font = XftFontOpenName(dpy, scr, "monospace:size=10");

	XftColorAllocName(dpy, vis, cmap, S_BG,     &col_bg);
	XftColorAllocName(dpy, vis, cmap, S_FG,     &col_fg);
	XftColorAllocName(dpy, vis, cmap, S_ACCENT, &col_accent);
	XftColorAllocName(dpy, vis, cmap, S_DIM,    &col_dim);
	XftColorAllocName(dpy, vis, cmap, S_WHITE,  &col_white);

	gc = XCreateGC(dpy, bar, 0, NULL);
	xd = XftDrawCreate(dpy, bar, vis, cmap);

	XMapRaised(dpy, bar);
	XSelectInput(dpy, root, PropertyChangeMask | SubstructureNotifyMask);

	update_status();
	update_desktop();
	update_occupied();
	update_title();
	draw();

	XEvent ev;
	for (;;) {
		XNextEvent(dpy, &ev);
		int dirty = 0;

		if (ev.type == Expose) {
			dirty = 1;
		} else if (ev.type == PropertyNotify) {
			Atom a = ev.xproperty.atom;
			if (ev.xproperty.window == root) {
				if (a == XA_WM_NAME || a == net_wm_name)
					{ update_status(); dirty = 1; }
				else if (a == net_cur_desk)
					{ update_desktop(); update_occupied(); dirty = 1; }
				else if (a == net_active)
					{ update_title(); dirty = 1; }
				else if (a == net_client_list)
					{ update_occupied(); dirty = 1; }
			} else {
				if (a == XA_WM_NAME || a == net_wm_name)
					{ update_title(); dirty = 1; }
			}
		}

		if (dirty) draw();
	}
}
