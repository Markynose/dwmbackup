#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>

int main(void) {
	Display *dpy = XOpenDisplay(NULL);
	if (!dpy) return 1;

	XkbDescPtr desc = XkbGetKeyboard(dpy, XkbAllComponentsMask, XkbUseCoreKbd);
	XkbStateRec state;
	XkbGetState(dpy, XkbUseCoreKbd, &state);

	if (desc && desc->names) {
		Atom grp = desc->names->groups[state.group];
		if (grp) {
			char *name = XGetAtomName(dpy, grp);
			if (name) {
				/* shorten: "English (US)" → "en", "Ukrainian" → "ua" */
				if (strstr(name, "kraini") || strstr(name, "kraïni"))
					printf("ua");
				else
					printf("en");
				XFree(name);
			}
		}
	}
	printf("\n");

	if (desc) XkbFreeKeyboard(desc, 0, True);
	XCloseDisplay(dpy);
	return 0;
}
