#include <string.h>
#include <stdlib.h>

#include <X11/Xatom.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>

#include "glitch.h"

extern WindowManager wm;

static void draw_switcher(void) {
	if (!wm.cycle_win || wm.cycle_count == 0) return;

	XSetWindowAttributes wa;
	wa.background_pixel = WhitePixel(wm.dpy, wm.screen);
	XChangeWindowAttributes(wm.dpy, wm.cycle_win, CWBackPixel, &wa);
	XClearWindow(wm.dpy, wm.cycle_win);

	int box_size = 100;
	int x_offset = 0;
	int y_offset = 0;

	for (int i = 0; i < wm.cycle_count; i++) {
		Window w = wm.cycle_clients[i];
		int is_selected = (i == wm.active_cycle_index);

		// Draw box background
		if (is_selected) {
			XSetForeground(wm.dpy, DefaultGC(wm.dpy, wm.screen), wm.xft_bg_color.pixel);
			XFillRectangle(wm.dpy, wm.cycle_win, DefaultGC(wm.dpy, wm.screen), x_offset, y_offset, box_size, box_size);
		} else {
			XSetForeground(wm.dpy, DefaultGC(wm.dpy, wm.screen), WhitePixel(wm.dpy, wm.screen));
			XFillRectangle(wm.dpy, wm.cycle_win, DefaultGC(wm.dpy, wm.screen), x_offset, y_offset, box_size, box_size);
		}

		// Get Program Name
		char *prog_name = NULL;
		XClassHint ch;
		if (XGetClassHint(wm.dpy, w, &ch)) {
			prog_name = ch.res_class;
			if (prog_name) {
				char *dash = strchr(prog_name, '-');
				if (dash) *dash = '\0';
			}
			if (ch.res_name) XFree(ch.res_name);
		}

		// Get Window Title
		char *win_title = NULL;
		if (!XFetchName(wm.dpy, w, &win_title)) {
			Atom utf8_string = XInternAtom(wm.dpy, "UTF8_STRING", False);
			XGetWindowProperty(wm.dpy, w, XInternAtom(wm.dpy, "_NET_WM_NAME", False), 0, (~0L), False, utf8_string, &(Atom){0}, &(int){0}, &(unsigned long){0}, &(unsigned long){0}, (unsigned char **)&win_title);
		}

		XftDraw *draw = XftDrawCreate(wm.dpy, wm.cycle_win, DefaultVisual(wm.dpy, wm.screen), wm.cmap);
		if (draw) {
			XftColor *color = is_selected ? &wm.xft_color : &wm.xft_root_bg_color;

			if (prog_name) {
				char truncated[9];
				strncpy(truncated, prog_name, 8);
				truncated[8] = '\0';
				XftDrawStringUtf8(draw, color, wm.font, x_offset + 10, y_offset + 70, (const FcChar8 *)truncated, strlen(truncated));
			}

			if (win_title) {
				char truncated[9];
				strncpy(truncated, win_title, 8);
				truncated[8] = '\0';
				XftDrawStringUtf8(draw, color, wm.font, x_offset + 10, y_offset + 90, (const FcChar8 *)truncated, strlen(truncated));
			}
			XftDrawDestroy(draw);
		}

		if (prog_name) XFree(prog_name);
		if (win_title) XFree(win_title);

		x_offset += box_size;
	}
	XFlush(wm.dpy);
}

void end_cycling(void) {
	if (!wm.is_cycling) return;

	wm.is_cycling = 0;
	if (wm.cycle_win) {
		XDestroyWindow(wm.dpy, wm.cycle_win);
		wm.cycle_win = None;
	}
	if (wm.cycle_clients) {
		free(wm.cycle_clients);
		wm.cycle_clients = NULL;
	}
	wm.cycle_count = 0;
	wm.active_cycle_index = -1;

	XUngrabKeyboard(wm.dpy, CurrentTime);
	log_message(stdout, LOG_DEBUG, "Ended window cycling");
}

void cycle_active_window(const Arg *arg) {
	// If not already cycling, initialize
	if (!wm.is_cycling) {
		wm.is_cycling = 1;

		// Grab keyboard to catch Alt release (key release)
		// We grab it on the root window. 
		XGrabKeyboard(wm.dpy, wm.root, True, GrabModeAsync, GrabModeAsync, CurrentTime);

		// Count clients
		int count = 0;
		Client *c = wm.clients;
		while (c) {
			count++;
			c = c->next;
		}

		if (count == 0) {
			end_cycling();
			return;
		}

		wm.cycle_clients = malloc(sizeof(Window) * count);
		wm.cycle_count = 0;
		wm.active_cycle_index = 0;

		// Filter for current desktop and mapped windows
		c = wm.clients;
		int current_window_index = -1;

		while (c) {
			Window w = c->window;

			unsigned long desktop = 0;
			Atom actual_type;
			int actual_format;
			unsigned long nitems, bytes_after;
			unsigned char *prop = NULL;

			XErrorHandler old = XSetErrorHandler(ignore_x_error);
			int status = XGetWindowProperty(wm.dpy, w, _NET_WM_DESKTOP, 0, 1, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
			XSync(wm.dpy, False);
			XSetErrorHandler(old);

			int on_current_desktop = 0;
			if (status == Success && prop && nitems > 0) {
				desktop = *(unsigned long *)prop;
				if (desktop == wm.current_desktop) {
					on_current_desktop = 1;
				}
			}
			if (prop) XFree(prop);
			if (is_sticky(w)) on_current_desktop = 1;

			if (on_current_desktop) {
				XWindowAttributes wa;
				XGetWindowAttributes(wm.dpy, w, &wa);
				if (wa.map_state == IsViewable) {
					wm.cycle_clients[wm.cycle_count] = w;
					if (w == wm.active) {
						current_window_index = wm.cycle_count;
					}
					wm.cycle_count++;
				}
			}
			c = c->next;
		}

		if (wm.cycle_count == 0) {
			end_cycling();
			return;
		}

		wm.active_cycle_index = (current_window_index + 1) % wm.cycle_count;

		// Create switcher window
		int box_size = 100;

		int width = (box_size * wm.cycle_count);
		int height = box_size;
		int screen_width = DisplayWidth(wm.dpy, wm.screen);
		int screen_height = DisplayHeight(wm.dpy, wm.screen);
		int x = (screen_width - width) / 2;
		int y = (screen_height * 2) / 3;

		XSetWindowAttributes wa;
		wa.override_redirect = True;
		wa.background_pixel = BlackPixel(wm.dpy, wm.screen);
		wa.border_pixel = BlackPixel(wm.dpy, wm.screen);

		wm.cycle_win = XCreateWindow(wm.dpy, wm.root, x, y, width, height, 0, CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixel | CWBorderPixel, &wa);

		XMapRaised(wm.dpy, wm.cycle_win);
	} else {
		// Already cycling, just move selection
		int delta = (arg->i == 0) ? 1 : -1;
		wm.active_cycle_index = (wm.active_cycle_index + delta + wm.cycle_count) % wm.cycle_count;
	}

	draw_switcher();
}
