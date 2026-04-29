/* chiwallpaper — set root window wallpaper; -g/--gif for animated GIF */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <Imlib2.h>

static Display *dpy;
static int      scr;
static Window   root;
static int      sw, sh;

static Pixmap render_image(Imlib_Image img) {
	imlib_context_set_image(img);
	int iw = imlib_image_get_width();
	int ih = imlib_image_get_height();

	double sx = (double)sw / iw;
	double sy = (double)sh / ih;
	double s  = sx > sy ? sx : sy;
	int nw = (int)(iw * s);
	int nh = (int)(ih * s);
	int ox = (sw - nw) / 2;
	int oy = (sh - nh) / 2;

	Imlib_Image scaled = imlib_create_cropped_scaled_image(0, 0, iw, ih, nw, nh);
	imlib_context_set_image(scaled);

	Pixmap pm = XCreatePixmap(dpy, root, sw, sh, DefaultDepth(dpy, scr));
	imlib_context_set_drawable(pm);

	GC gc = DefaultGC(dpy, scr);
	XSetForeground(dpy, gc, BlackPixel(dpy, scr));
	XFillRectangle(dpy, pm, gc, 0, 0, sw, sh);
	imlib_render_image_on_drawable(ox, oy);
	imlib_free_image();
	return pm;
}

static void set_root(Pixmap pm) {
	XSetWindowBackgroundPixmap(dpy, root, pm);
	XClearWindow(dpy, root);
	Atom a1 = XInternAtom(dpy, "_XROOTPMAP_ID",   False);
	Atom a2 = XInternAtom(dpy, "ESETROOT_PMAP_ID", False);
	XChangeProperty(dpy, root, a1, XA_PIXMAP, 32, PropModeReplace, (unsigned char *)&pm, 1);
	XChangeProperty(dpy, root, a2, XA_PIXMAP, 32, PropModeReplace, (unsigned char *)&pm, 1);
	XFlush(dpy);
}

int main(int argc, char *argv[]) {
	int         gif_mode = 0;
	const char *file     = NULL;

	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-g") || !strcmp(argv[i], "--gif"))
			gif_mode = 1;
		else
			file = argv[i];
	}

	if (!file) {
		fprintf(stderr, "usage: chiwallpaper [-g|--gif] <image>\n");
		return 1;
	}

	dpy  = XOpenDisplay(NULL);
	if (!dpy) { fprintf(stderr, "chiwallpaper: cannot open display\n"); return 1; }
	scr  = DefaultScreen(dpy);
	root = RootWindow(dpy, scr);
	sw   = DisplayWidth(dpy, scr);
	sh   = DisplayHeight(dpy, scr);

	imlib_context_set_display(dpy);
	imlib_context_set_visual(DefaultVisual(dpy, scr));
	imlib_context_set_colormap(DefaultColormap(dpy, scr));

	/* load frame 1 to get metadata */
	Imlib_Image img = imlib_load_image_frame(file, 1);
	if (!img) {
		fprintf(stderr, "chiwallpaper: cannot load: %s\n", file);
		XCloseDisplay(dpy);
		return 1;
	}

	imlib_context_set_image(img);
	Imlib_Frame_Info info = {0};
	imlib_image_get_frame_info(&info);
	int frames = info.frame_count;

	if (!gif_mode || frames <= 1) {
		Pixmap pm = render_image(img);
		imlib_free_image();
		set_root(pm);
		XCloseDisplay(dpy);
		return 0;
	}

	/* animated GIF — runs forever, background it in xinitrc */
	Pixmap prev = 0;
	for (;;) {
		for (int f = 1; f <= frames; f++) {
			img = imlib_load_image_frame(file, f);
			if (!img) continue;

			imlib_context_set_image(img);
			imlib_image_get_frame_info(&info);
			int delay_ms = info.frame_delay > 0 ? info.frame_delay : 80;

			Pixmap pm = render_image(img);
			imlib_free_image();
			set_root(pm);
			if (prev) XFreePixmap(dpy, prev);
			prev = pm;

			usleep((unsigned int)delay_ms * 1000);
		}
	}

	XCloseDisplay(dpy);
	return 0;
}
