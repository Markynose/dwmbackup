/* chiview — minimal image viewer using imlib2 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <Imlib2.h>

static Display *dpy;
static Window   win;
static int      scr;
static GC       gc;

static int      win_w, win_h;
static int      img_w, img_h;
static double   zoom = 1.0;
static int      pan_x = 0, pan_y = 0;
static int      drag = 0, drag_x, drag_y, drag_px, drag_py;

static Imlib_Image img = NULL;

static void render(void) {
	if (!img) return;

	int dw = (int)(img_w * zoom);
	int dh = (int)(img_h * zoom);
	int ox = pan_x + (win_w - dw) / 2;
	int oy = pan_y + (win_h - dh) / 2;

	XSetForeground(dpy, gc, BlackPixel(dpy, scr));
	XFillRectangle(dpy, win, gc, 0, 0, win_w, win_h);

	imlib_context_set_image(img);
	imlib_context_set_drawable(win);
	imlib_render_image_on_drawable_at_size(ox, oy, dw, dh);
}

static void fit_to_window(void) {
	double sx = (double)win_w / img_w;
	double sy = (double)win_h / img_h;
	zoom = sx < sy ? sx : sy;
	pan_x = 0; pan_y = 0;
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "usage: chiview <image>\n");
		return 1;
	}

	dpy = XOpenDisplay(NULL);
	if (!dpy) { fprintf(stderr, "chiview: cannot open display\n"); return 1; }
	scr = DefaultScreen(dpy);

	img = imlib_load_image(argv[1]);
	if (!img) { fprintf(stderr, "chiview: cannot load: %s\n", argv[1]); return 1; }

	imlib_context_set_image(img);
	img_w = imlib_image_get_width();
	img_h = imlib_image_get_height();

	/* initial window size: fit to screen but not larger than image */
	int sw = DisplayWidth(dpy, scr);
	int sh = DisplayHeight(dpy, scr);
	win_w = img_w < sw ? img_w : sw * 9 / 10;
	win_h = img_h < sh ? img_h : sh * 9 / 10;

	win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr),
		(sw - win_w) / 2, (sh - win_h) / 2, win_w, win_h,
		0, 0, BlackPixel(dpy, scr));

	/* set title */
	char *fname = strrchr(argv[1], '/');
	XStoreName(dpy, win, fname ? fname + 1 : argv[1]);

	XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask |
		ButtonReleaseMask | PointerMotionMask | StructureNotifyMask | Button4MotionMask);

	gc = XCreateGC(dpy, win, 0, NULL);

	imlib_context_set_display(dpy);
	imlib_context_set_visual(DefaultVisual(dpy, scr));
	imlib_context_set_colormap(DefaultColormap(dpy, scr));

	XMapWindow(dpy, win);

	fit_to_window();

	Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &wm_delete, 1);

	XEvent ev;
	while (1) {
		XNextEvent(dpy, &ev);
		switch (ev.type) {
		case Expose:
			render();
			break;
		case ConfigureNotify:
			win_w = ev.xconfigure.width;
			win_h = ev.xconfigure.height;
			fit_to_window();
			render();
			break;
		case KeyPress: {
			KeySym ks = XLookupKeysym(&ev.xkey, 0);
			switch (ks) {
			case XK_q: case XK_Escape:
				goto done;
			case XK_equal: case XK_plus:
				zoom *= 1.2; render(); break;
			case XK_minus:
				zoom /= 1.2; if (zoom < 0.05) zoom = 0.05; render(); break;
			case XK_0:
				fit_to_window(); render(); break;
			case XK_1:
				zoom = 1.0; pan_x = 0; pan_y = 0; render(); break;
			case XK_h: case XK_Left:
				pan_x += 50; render(); break;
			case XK_l: case XK_Right:
				pan_x -= 50; render(); break;
			case XK_k: case XK_Up:
				pan_y += 50; render(); break;
			case XK_j: case XK_Down:
				pan_y -= 50; render(); break;
			}
			break;
		}
		case ButtonPress:
			if (ev.xbutton.button == Button1) {
				drag = 1;
				drag_x = ev.xbutton.x; drag_y = ev.xbutton.y;
				drag_px = pan_x; drag_py = pan_y;
			} else if (ev.xbutton.button == Button4) {
				zoom *= 1.1; render();
			} else if (ev.xbutton.button == Button5) {
				zoom /= 1.1; if (zoom < 0.05) zoom = 0.05; render();
			}
			break;
		case ButtonRelease:
			if (ev.xbutton.button == Button1) drag = 0;
			break;
		case MotionNotify:
			if (drag) {
				pan_x = drag_px + (ev.xmotion.x - drag_x);
				pan_y = drag_py + (ev.xmotion.y - drag_y);
				render();
			}
			break;
		case ClientMessage:
			if ((Atom)ev.xclient.data.l[0] == wm_delete) goto done;
			break;
		}
	}
done:
	imlib_free_image();
	XFreeGC(dpy, gc);
	XCloseDisplay(dpy);
	return 0;
}
