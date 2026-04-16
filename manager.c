#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/cursorfont.h>
#include <X11/Xproto.h>

#include "glitch.h"
#include "config.h"

extern WindowManager wm;

Atom _NET_WM_DESKTOP;
static Atom _NET_CURRENT_DESKTOP;
static Atom _NET_NUMBER_OF_DESKTOPS;
static Atom _NET_CLIENT_LIST;
static Atom _NET_WM_STATE;
static Atom _NET_WM_NAME;
static Atom _NET_SUPPORTING_WM_CHECK;
static Atom _NET_WM_STATE_FULLSCREEN;
static Atom _NET_ACTIVE_WINDOW;
static Atom _NET_WM_STATE_STICKY;
static Atom _NET_WM_STATE_MAXIMIZED_HORZ;
static Atom _NET_WM_STATE_MAXIMIZED_VERT;
static Atom _NET_WM_STATE_ABOVE;
static Atom _GLITCH_PRE_HMAX_GEOM;
static Atom _GLITCH_PRE_VMAX_GEOM;
static Atom _GLITCH_PRE_FULLSCREEN_GEOM;
static Atom _MOTIF_WM_HINTS;
static Atom WM_PROTOCOLS;
static Atom WM_DELETE_WINDOW;
static Atom WM_TAKE_FOCUS;
static Atom _NET_SUPPORTED;
static Atom _NET_FRAME_EXTENTS;
static Atom WM_STATE_ATOM;

static void update_window_border(Window window, int active) {
	if (window == None) return;
	if (!window_exists(window)) return;

	Atom actual_type;
	int actual_format;
	unsigned long nitems, bytes_after;
	unsigned char *prop = NULL;
	int has_decorations = 1;

	// Check _MOTIF_WM_HINTS to see if the window requested no decorations
	XErrorHandler old = XSetErrorHandler(ignore_x_error);
	int status = XGetWindowProperty(wm.dpy, window, _MOTIF_WM_HINTS, 0, 5, False, AnyPropertyType,
			&actual_type, &actual_format, &nitems, &bytes_after, &prop);
	XSync(wm.dpy, False);
	XSetErrorHandler(old);

	if (status == Success) {
		if (prop && nitems >= 3) {
			unsigned long flags = ((unsigned long *)prop)[0];
			unsigned long decorations = ((unsigned long *)prop)[2];
			// If flags bit 1 is set (MWM_HINTS_DECORATIONS) and decorations bit 0 is cleared (MWM_DECOR_ALL/BORDER), then no border.
			// Simplification: if decorations is 0, assume no border.
			if ((flags & 2) && (decorations & 1) == 0) {
				has_decorations = 0;
			}
		}
		if (prop) XFree(prop);
	}

	unsigned int bw = has_decorations ? border_size : 0;
	XSetWindowBorderWidth(wm.dpy, window, bw);

	if (active) {
		if (is_always_on_top(window)) {
			XSetWindowBorder(wm.dpy, window, wm.borders.on_top_active);
		} else if (is_sticky(window)) {
			XSetWindowBorder(wm.dpy, window, wm.borders.sticky_active);
		} else {
			XSetWindowBorder(wm.dpy, window, wm.borders.normal_active);
		}
	} else {
		if (is_always_on_top(window)) {
			XSetWindowBorder(wm.dpy, window, wm.borders.on_top_inactive);
		} else if (is_sticky(window)) {
			XSetWindowBorder(wm.dpy, window, wm.borders.sticky_inactive);
		} else {
			XSetWindowBorder(wm.dpy, window, wm.borders.normal_inactive);
		}
	}
}

static void update_wm_state(Window w, Atom state_atom, int add);
static int has_wm_state(Window w, Atom state_atom);
static void check_and_clear_maximized_state(Window w, int horizontal, int vertical);
static void add_client(Window w);
static void remove_client(Window w);
static Window get_toplevel_window(Window w);
static void set_fullscreen(Window w, int full);
static void send_configure(Window w);
static Client *wintoclient(Window w);

int x_error_handler(Display *dpy, XErrorEvent *ee) {
	(void) dpy;

	if (ee->error_code == BadWindow ||
			(ee->request_code == X_SetInputFocus && ee->error_code == BadMatch) ||
			(ee->request_code == X_PolyText8 && ee->error_code == BadDrawable) ||
			(ee->request_code == X_PolyFillRectangle && ee->error_code == BadDrawable) ||
			(ee->request_code == X_PolySegment && ee->error_code == BadDrawable) ||
			(ee->request_code == X_ConfigureWindow && ee->error_code == BadMatch) ||
			(ee->request_code == X_GrabButton && ee->error_code == BadAccess) ||
			(ee->request_code == X_GrabKey && ee->error_code == BadAccess) ||
			(ee->request_code == X_CopyArea && ee->error_code == BadDrawable)) {
		return 0;
	}
	log_message(stderr, LOG_ERROR, "Fatal X Error: request_code=%d, error_code=%d, resource_id=0x%lx", ee->request_code, ee->error_code, ee->resourceid);
	return 0;
}

static void set_client_state(Window w, long state) {
	long data[] = { state, None };
	XChangeProperty(wm.dpy, w, WM_STATE_ATOM, WM_STATE_ATOM, 32, PropModeReplace, (unsigned char *)data, 2);
}

static void add_client(Window w) {
	// Check if already in list or is root.
	if (w == wm.root) return;
	Client *c = wm.clients;
	while (c) {
		if (c->window == w) return;
		c = c->next;
	}

	Client *new_c = malloc(sizeof(Client));
	if (!new_c) return;

	new_c->window = w;
	new_c->next = wm.clients;
	new_c->prev = NULL;
	new_c->saved_x = 0;
	new_c->saved_y = 0;
	new_c->saved_w = 0;
	new_c->saved_h = 0;
	new_c->has_saved_state = 0;

	if (wm.clients) {
		wm.clients->prev = new_c;
	}
	wm.clients = new_c;
	log_message(stdout, LOG_DEBUG, "Added client 0x%lx", w);

	unsigned long extents[4] = {0, 0, 0, 0};
	XChangeProperty(wm.dpy, w, _NET_FRAME_EXTENTS, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)extents, 4);
	set_client_state(w, NormalState);
}

static void remove_client(Window w) {
	Client *c = wm.clients;
	while (c) {
		if (c->window == w) {
			if (c->prev) {
				c->prev->next = c->next;
			} else {
				wm.clients = c->next;
			}

			if (c->next) {
				c->next->prev = c->prev;
			}

			set_client_state(w, WithdrawnState);
			free(c);
			log_message(stdout, LOG_DEBUG, "Removed client 0x%lx", w);
			return;
		}
		c = c->next;
	}
}

static Window get_toplevel_window(Window w) {
	if (w == None || w == wm.root) return None;

	Client *c = wm.clients;
	while (c) {
		if (c->window == w) return w;
		c = c->next;
	}

	Window root, parent, *children;
	unsigned int nchildren;
	if (XQueryTree(wm.dpy, w, &root, &parent, &children, &nchildren)) {
		if (children) XFree(children);
		if (parent == root || parent == None) return None;
		return get_toplevel_window(parent);
	}

	return None;
}

static void scan_windows(void) {
	unsigned int nwins;
	Window d1, d2, *wins;
	XWindowAttributes wa;

	if (XQueryTree(wm.dpy, wm.root, &d1, &d2, &wins, &nwins)) {
		for (unsigned int i = 0; i < nwins; i++) {
			if (XGetWindowAttributes(wm.dpy, wins[i], &wa)
					&& !wa.override_redirect && (wa.map_state == IsViewable || wa.map_state == IsUnmapped)) {
				add_client(wins[i]);
				XSelectInput(wm.dpy, wins[i], EnterWindowMask | LeaveWindowMask);
				grab_buttons(wins[i]);
				update_window_border(wins[i], 0);
			}
		}
		if (wins) XFree(wins);
	}

	// Restore focus.
	Window focus_win;
	int revert_to;
	XGetInputFocus(wm.dpy, &focus_win, &revert_to);
	if (focus_win != None && focus_win != wm.root) {
		Window toplevel = get_toplevel_window(focus_win);
		if (toplevel != None && window_exists(toplevel)) {
			set_active_window(toplevel, CurrentTime);
			set_active_border(toplevel);
		}
	}
}

void init_window_manager(void) {
	wm.dpy = XOpenDisplay(NULL);
	if (!wm.dpy) {
		log_message(stdout, LOG_ERROR, "Cannot open display");
		abort();
	}

	XSetErrorHandler(x_error_handler);

	wm.screen = DefaultScreen(wm.dpy);
	wm.root = RootWindow(wm.dpy, wm.screen);
	XSetWindowBackground(wm.dpy, wm.root, BlackPixel(wm.dpy, wm.screen));
	XClearWindow(wm.dpy, wm.root);

	// Create and sets up cursors.
	wm.cursor_default = XCreateFontCursor(wm.dpy, XC_left_ptr);
	wm.cursor_move = XCreateFontCursor(wm.dpy, XC_fleur);
	wm.cursor_resize  = XCreateFontCursor(wm.dpy, XC_sizing);
	XDefineCursor(wm.dpy, wm.root, wm.cursor_default);
	log_message(stdout, LOG_DEBUG, "Setting up default cursors");

	// Root window input selection masks.
	XSelectInput(wm.dpy, wm.root,
			SubstructureRedirectMask | SubstructureNotifyMask |
			FocusChangeMask | EnterWindowMask | LeaveWindowMask |
			ButtonPressMask | ExposureMask | PropertyChangeMask);

	// Initialize EWMH atoms.
	_NET_WM_DESKTOP = XInternAtom(wm.dpy, "_NET_WM_DESKTOP", False);
	_NET_CURRENT_DESKTOP = XInternAtom(wm.dpy, "_NET_CURRENT_DESKTOP", False);
	_NET_NUMBER_OF_DESKTOPS = XInternAtom(wm.dpy, "_NET_NUMBER_OF_DESKTOPS", False);
	_NET_CLIENT_LIST = XInternAtom(wm.dpy, "_NET_CLIENT_LIST", False);
	_NET_WM_STATE = XInternAtom(wm.dpy, "_NET_WM_STATE", False);
	_NET_WM_STATE_FULLSCREEN = XInternAtom(wm.dpy, "_NET_WM_STATE_FULLSCREEN", False);
	_NET_ACTIVE_WINDOW = XInternAtom(wm.dpy, "_NET_ACTIVE_WINDOW", False);
	_NET_WM_STATE_STICKY = XInternAtom(wm.dpy, "_NET_WM_STATE_STICKY", False);
	_NET_WM_STATE_MAXIMIZED_HORZ = XInternAtom(wm.dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
	_NET_WM_STATE_MAXIMIZED_VERT = XInternAtom(wm.dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
	_NET_WM_STATE_ABOVE = XInternAtom(wm.dpy, "_NET_WM_STATE_ABOVE", False);
	_NET_SUPPORTED = XInternAtom(wm.dpy, "_NET_SUPPORTED", False);
	_NET_SUPPORTING_WM_CHECK = XInternAtom(wm.dpy, "_NET_SUPPORTING_WM_CHECK", False);
	_NET_WM_NAME = XInternAtom(wm.dpy, "_NET_WM_NAME", False);
	_NET_FRAME_EXTENTS = XInternAtom(wm.dpy, "_NET_FRAME_EXTENTS", False);
	WM_STATE_ATOM = XInternAtom(wm.dpy, "WM_STATE", False);

	// Create supporting window for EWMH compliance.
	XSetWindowAttributes wa;
	wa.override_redirect = True;
	Window check_win = XCreateWindow(wm.dpy, wm.root, -1, -1, 1, 1, 0,
			CopyFromParent, InputOutput, CopyFromParent,
			CWOverrideRedirect, &wa);
	XMapWindow(wm.dpy, check_win);
	XChangeProperty(wm.dpy, check_win, _NET_SUPPORTING_WM_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check_win, 1);
	XChangeProperty(wm.dpy, check_win, _NET_WM_NAME, XA_STRING, 8, PropModeReplace, (unsigned char *)"LG3D", 4);
	XChangeProperty(wm.dpy, wm.root, _NET_SUPPORTING_WM_CHECK, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&check_win, 1);
	XChangeProperty(wm.dpy, wm.root, _NET_WM_NAME, XA_STRING, 8, PropModeReplace, (unsigned char *)"LG3D", 4);

	// Set supported atoms.
	Atom net_atoms[] = {
		_NET_SUPPORTED,
		_NET_SUPPORTING_WM_CHECK,
		_NET_WM_NAME,
		_NET_FRAME_EXTENTS,
		_NET_WM_DESKTOP,
		_NET_CURRENT_DESKTOP,
		_NET_NUMBER_OF_DESKTOPS,
		_NET_CLIENT_LIST,
		_NET_WM_STATE,
		_NET_WM_STATE_FULLSCREEN,
		_NET_ACTIVE_WINDOW,
		_NET_WM_STATE_STICKY,
		_NET_WM_STATE_MAXIMIZED_HORZ,
		_NET_WM_STATE_MAXIMIZED_VERT,
		_NET_WM_STATE_ABOVE,
		WM_STATE_ATOM,
		WM_DELETE_WINDOW,
		WM_TAKE_FOCUS
	};
	XChangeProperty(wm.dpy, wm.root, _NET_SUPPORTED, XA_ATOM, 32, PropModeReplace, (unsigned char *)net_atoms, LENGTH(net_atoms));

	// Set number of desktops and current desktop.
	static unsigned long num_desktops = NUM_DESKTOPS;
	static unsigned long current_desktop = 1;
	wm.current_desktop = 1;
	XChangeProperty(wm.dpy, wm.root, _NET_NUMBER_OF_DESKTOPS, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&num_desktops, 1);
	XChangeProperty(wm.dpy, wm.root, _NET_CURRENT_DESKTOP, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&current_desktop, 1);
	log_message(stdout, LOG_DEBUG, "Registering %d desktops", NUM_DESKTOPS);

	// Initialize layout modes.
	for (int i = 0; i <= NUM_DESKTOPS; i++) {
		wm.layout_modes[i] = LAYOUT_FLOATING;
	}

	// Initialize colormap early as it's needed for Xft.
	wm.cmap = DefaultColormap(wm.dpy, wm.screen);

	// Setup Xft font and drawing.
	wm.font = XftFontOpenName(wm.dpy, wm.screen, widget_font);
	if (!wm.font) {
		log_message(stdout, LOG_WARNING, "Failed to load font %s, falling back to fixed", widget_font);
		wm.font = XftFontOpenName(wm.dpy, wm.screen, "fixed");
	}

	wm.launcher_font = XftFontOpenName(wm.dpy, wm.screen, launcher_font_name);
	if (!wm.launcher_font) {
		log_message(stdout, LOG_WARNING, "Failed to load launcher font %s, falling back to fixed", launcher_font_name);
		wm.launcher_font = XftFontOpenName(wm.dpy, wm.screen, "fixed");
	}

	Visual *visual = DefaultVisual(wm.dpy, wm.screen);

	// Create XftDraw for the root window.
	wm.xft_draw = XftDrawCreate(wm.dpy, wm.root, visual, wm.cmap);

	if (!XftColorAllocName(wm.dpy, visual, wm.cmap, indicator_fg_color, &wm.xft_color)) {
		log_message(stdout, LOG_WARNING, "Failed to allocate color %s, falling back to white", indicator_fg_color);
		XRenderColor render_color = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
		XftColorAllocValue(wm.dpy, visual, wm.cmap, &render_color, &wm.xft_color);
	}

	if (!XftColorAllocName(wm.dpy, visual, wm.cmap, indicator_bg_color, &wm.xft_bg_color)) {
		log_message(stdout, LOG_WARNING, "Failed to allocate color %s, falling back to black", indicator_bg_color);
		XRenderColor render_color = {0x0000, 0x0000, 0x0000, 0xFFFF};
		XftColorAllocValue(wm.dpy, visual, wm.cmap, &render_color, &wm.xft_bg_color);
	}

	// Root background color (black) for widgets to blend in.
	XRenderColor black_render = {0x0000, 0x0000, 0x0000, 0xFFFF};
	XftColorAllocValue(wm.dpy, visual, wm.cmap, &black_render, &wm.xft_root_bg_color);

	if (!XftColorAllocName(wm.dpy, visual, wm.cmap, mic_active_bg_color, &wm.xft_mic_active_bg)) {
		log_message(stdout, LOG_WARNING, "Failed to allocate color %s, falling back to orange", mic_active_bg_color);
		XRenderColor render_color = {0xFFFF, 0x8000, 0x0000, 0xFFFF};
		XftColorAllocValue(wm.dpy, visual, wm.cmap, &render_color, &wm.xft_mic_active_bg);
	}

	if (!XftColorAllocName(wm.dpy, visual, wm.cmap, mic_muted_bg_color, &wm.xft_mic_muted_bg)) {
		log_message(stdout, LOG_WARNING, "Failed to allocate color %s, falling back to dark gray", mic_muted_bg_color);
		XRenderColor render_color = {0x4444, 0x4444, 0x4444, 0xFFFF};
		XftColorAllocValue(wm.dpy, visual, wm.cmap, &render_color, &wm.xft_mic_muted_bg);
	}

	if (!XftColorAllocName(wm.dpy, visual, wm.cmap, mic_active_fg_color, &wm.xft_mic_active_fg)) {
		log_message(stdout, LOG_WARNING, "Failed to allocate color %s, falling back to white", mic_active_fg_color);
		XRenderColor render_color = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
		XftColorAllocValue(wm.dpy, visual, wm.cmap, &render_color, &wm.xft_mic_active_fg);
	}

	if (!XftColorAllocName(wm.dpy, visual, wm.cmap, mic_muted_fg_color, &wm.xft_mic_muted_fg)) {
		log_message(stdout, LOG_WARNING, "Failed to allocate color %s, falling back to white", mic_muted_fg_color);
		XRenderColor render_color = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
		XftColorAllocValue(wm.dpy, visual, wm.cmap, &render_color, &wm.xft_mic_muted_fg);
	}

	if (!XftColorAllocName(wm.dpy, visual, wm.cmap, layout_tile_bg_color, &wm.xft_layout_tile_bg)) {
		log_message(stdout, LOG_WARNING, "Failed to allocate color %s, falling back to dark green", layout_tile_bg_color);
		XRenderColor render_color = {0x0000, 0x6400, 0x0000, 0xFFFF};
		XftColorAllocValue(wm.dpy, visual, wm.cmap, &render_color, &wm.xft_layout_tile_bg);
	}

	if (!XftColorAllocName(wm.dpy, visual, wm.cmap, layout_float_bg_color, &wm.xft_layout_float_bg)) {
		log_message(stdout, LOG_WARNING, "Failed to allocate color %s, falling back to gray", layout_float_bg_color);
		XRenderColor render_color = {0x3333, 0x3333, 0x3333, 0xFFFF};
		XftColorAllocValue(wm.dpy, visual, wm.cmap, &render_color, &wm.xft_layout_float_bg);
	}

	if (!XftColorAllocName(wm.dpy, visual, wm.cmap, layout_tile_fg_color, &wm.xft_layout_tile_fg)) {
		log_message(stdout, LOG_WARNING, "Failed to allocate color %s, falling back to white", layout_tile_fg_color);
		XRenderColor render_color = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
		XftColorAllocValue(wm.dpy, visual, wm.cmap, &render_color, &wm.xft_layout_tile_fg);
	}

	if (!XftColorAllocName(wm.dpy, visual, wm.cmap, layout_float_fg_color, &wm.xft_layout_float_fg)) {
		log_message(stdout, LOG_WARNING, "Failed to allocate color %s, falling back to white", layout_float_fg_color);
		XRenderColor render_color = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
		XftColorAllocValue(wm.dpy, visual, wm.cmap, &render_color, &wm.xft_layout_float_fg);
	}

	if (!XftColorAllocName(wm.dpy, visual, wm.cmap, launcher_bg_color, &wm.xft_launcher_bg)) {
		XRenderColor render_color = {0x0000, 0x0000, 0x0000, 0xFFFF};
		XftColorAllocValue(wm.dpy, visual, wm.cmap, &render_color, &wm.xft_launcher_bg);
	}
	if (!XftColorAllocName(wm.dpy, visual, wm.cmap, launcher_border_color, &wm.xft_launcher_border)) {
		XRenderColor render_color = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
		XftColorAllocValue(wm.dpy, visual, wm.cmap, &render_color, &wm.xft_launcher_border);
	}
	if (!XftColorAllocName(wm.dpy, visual, wm.cmap, launcher_fg_color, &wm.xft_launcher_fg)) {
		XRenderColor render_color = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
		XftColorAllocValue(wm.dpy, visual, wm.cmap, &render_color, &wm.xft_launcher_fg);
	}
	if (!XftColorAllocName(wm.dpy, visual, wm.cmap, launcher_hl_bg_color, &wm.xft_launcher_hl_bg)) {
		XRenderColor render_color = {0x8000, 0x8000, 0x0000, 0xFFFF};
		XftColorAllocValue(wm.dpy, visual, wm.cmap, &render_color, &wm.xft_launcher_hl_bg);
	}
	if (!XftColorAllocName(wm.dpy, visual, wm.cmap, launcher_hl_fg_color, &wm.xft_launcher_hl_fg)) {
		XRenderColor render_color = {0x0000, 0x0000, 0x0000, 0xFFFF};
		XftColorAllocValue(wm.dpy, visual, wm.cmap, &render_color, &wm.xft_launcher_hl_fg);
	}

	wm.running = 1;

	// Grab keys for keybinds.
	for (unsigned int i = 0; i < LENGTH(keybinds); i++) {
		KeyCode keycode = XKeysymToKeycode(wm.dpy, keybinds[i].keysym);
		if (keycode) {
			XGrabKey(wm.dpy, keycode, keybinds[i].mod, wm.root, True, GrabModeAsync, GrabModeAsync);
			log_message(stdout, LOG_DEBUG, "Grabbed key: mod=0x%x, keysym=0x%lx", keybinds[i].mod, keybinds[i].keysym);
		}
	}

	// Grab keys for shortcuts.
	for (unsigned int i = 0; i < LENGTH(shortcuts); i++) {
		KeyCode keycode = XKeysymToKeycode(wm.dpy, shortcuts[i].keysym);
		if (keycode) {
			XGrabKey(wm.dpy, keycode, shortcuts[i].mod, wm.root, True, GrabModeAsync, GrabModeAsync);
			log_message(stdout, LOG_DEBUG, "Grabbed shortcut: mod=0x%x, keysym=0x%lx, command=%s", shortcuts[i].mod, shortcuts[i].keysym, shortcuts[i].cmd);
		}
	}

	// Grab keys for window dragging (with MODKEY).
	XGrabButton(wm.dpy, 1, MODKEY, wm.root, True, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
	XGrabButton(wm.dpy, 3, MODKEY, wm.root, True, ButtonPressMask|ButtonReleaseMask|PointerMotionMask, GrabModeAsync, GrabModeAsync, None, None);
	log_message(stdout, LOG_DEBUG, "Registering grab keys for window dragging");

	// Prepare border colors.
	XColor active_color, inactive_color, sticky_active_color, sticky_inactive_color, dummy;

	wm.borders.normal_active = BlackPixel(wm.dpy, wm.screen);
	wm.borders.normal_inactive = BlackPixel(wm.dpy, wm.screen);
	wm.borders.sticky_active = BlackPixel(wm.dpy, wm.screen);
	wm.borders.sticky_inactive = BlackPixel(wm.dpy, wm.screen);

	if (XAllocNamedColor(wm.dpy, wm.cmap, active_border_color, &active_color, &dummy)) {
		wm.borders.normal_active = active_color.pixel;
	}

	if (XAllocNamedColor(wm.dpy, wm.cmap, inactive_border_color, &inactive_color, &dummy)) {
		wm.borders.normal_inactive = inactive_color.pixel;
	}

	if (XAllocNamedColor(wm.dpy, wm.cmap, sticky_active_border_color, &sticky_active_color, &dummy)) {
		wm.borders.sticky_active = sticky_active_color.pixel;
	}

	if (XAllocNamedColor(wm.dpy, wm.cmap, sticky_inactive_border_color, &sticky_inactive_color, &dummy)) {
		wm.borders.sticky_inactive = sticky_inactive_color.pixel;
	}

	XColor on_top_active, on_top_inactive;
	wm.borders.on_top_active = BlackPixel(wm.dpy, wm.screen);
	wm.borders.on_top_inactive = BlackPixel(wm.dpy, wm.screen);

	if (XAllocNamedColor(wm.dpy, wm.cmap, on_top_active_border_color, &on_top_active, &dummy)) {
		wm.borders.on_top_active = on_top_active.pixel;
	}
	if (XAllocNamedColor(wm.dpy, wm.cmap, on_top_inactive_border_color, &on_top_inactive, &dummy)) {
		wm.borders.on_top_inactive = on_top_inactive.pixel;
	}

	// Scan for existing windows and apply initial layout.
	scan_windows();
	apply_tiling_layout();

	redraw_widgets();
	update_client_list();
	init_audio();
	XSync(wm.dpy, False);
}

void execute_shortcut(const char *command) {
	if (!command || strlen(command) == 0) {
		log_message(stdout, LOG_WARNING, "Empty command provided to execute_shortcut");
		return;
	}

	pid_t pid = fork();
	if (pid == -1) {
		log_message(stdout, LOG_ERROR, "Failed to fork process for command: %s", command);
		return;
	}

	if (pid == 0) {
		if (wm.dpy) close(ConnectionNumber(wm.dpy));
		setsid();
		execl("/bin/sh", "sh", "-c", command, (char *)NULL);
		log_message(stderr, LOG_ERROR, "Failed to execute command: %s", command);
		exit(1);
	} else {
		log_message(stdout, LOG_DEBUG, "Executed command in background: %s", command);
	}
}

void deinit_window_manager(void) {
	deinit_audio();
	XftColorFree(wm.dpy, DefaultVisual(wm.dpy, wm.screen), wm.cmap, &wm.xft_color);
	XftColorFree(wm.dpy, DefaultVisual(wm.dpy, wm.screen), wm.cmap, &wm.xft_bg_color);
	XftColorFree(wm.dpy, DefaultVisual(wm.dpy, wm.screen), wm.cmap, &wm.xft_root_bg_color);
	XftColorFree(wm.dpy, DefaultVisual(wm.dpy, wm.screen), wm.cmap, &wm.xft_mic_active_bg);
	XftColorFree(wm.dpy, DefaultVisual(wm.dpy, wm.screen), wm.cmap, &wm.xft_mic_muted_bg);
	XftColorFree(wm.dpy, DefaultVisual(wm.dpy, wm.screen), wm.cmap, &wm.xft_mic_active_fg);
	XftColorFree(wm.dpy, DefaultVisual(wm.dpy, wm.screen), wm.cmap, &wm.xft_mic_muted_fg);

	XftColorFree(wm.dpy, DefaultVisual(wm.dpy, wm.screen), wm.cmap, &wm.xft_launcher_bg);
	XftColorFree(wm.dpy, DefaultVisual(wm.dpy, wm.screen), wm.cmap, &wm.xft_launcher_border);
	XftColorFree(wm.dpy, DefaultVisual(wm.dpy, wm.screen), wm.cmap, &wm.xft_launcher_fg);
	XftColorFree(wm.dpy, DefaultVisual(wm.dpy, wm.screen), wm.cmap, &wm.xft_launcher_hl_bg);
	XftColorFree(wm.dpy, DefaultVisual(wm.dpy, wm.screen), wm.cmap, &wm.xft_launcher_hl_fg);

	XftDrawDestroy(wm.xft_draw);

	if (wm.launcher_win) XDestroyWindow(wm.dpy, wm.launcher_win);
	if (wm.launcher_items) {
		for (int i = 0; i < wm.launcher_items_count; i++) {
			free(wm.launcher_items[i].name);
			free(wm.launcher_items[i].exec);
		}
		free(wm.launcher_items);
	}
	if (wm.launcher_filtered) free(wm.launcher_filtered);

	XftFontClose(wm.dpy, wm.font);
	XftFontClose(wm.dpy, wm.launcher_font);
	XFreeCursor(wm.dpy, wm.cursor_default);
	XFreeCursor(wm.dpy, wm.cursor_move);
	XFreeCursor(wm.dpy, wm.cursor_resize);
}

int is_always_on_top(Window window) {
	if (window == None) return 0;
	return has_wm_state(window, _NET_WM_STATE_ABOVE);
}

void raise_window(Window window) {
	if (window == None) return;
	if (!window_exists(window)) return;

	// If the window is already always-on-top, just raise it to the absolute top.
	if (is_always_on_top(window)) {
		XRaiseWindow(wm.dpy, window);
		return;
	}

	// Otherwise, find the lowest "always-on-top" window and stack this window just below it.
	// If no "always-on-top" window exists, raise to top.
	Window root_return, parent_return, *children;
	unsigned int nchildren;
	if (XQueryTree(wm.dpy, wm.root, &root_return, &parent_return, &children, &nchildren)) {
		// Traverse from bottom to top.
		for (unsigned int i = 0; i < nchildren; i++) {
			if (children[i] == window) continue;

			if (is_always_on_top(children[i])) {
				// Found the first (lowest) always-on-top window.
				// Stack 'window' below 'children[i]'.
				XWindowChanges changes;
				changes.sibling = children[i];
				changes.stack_mode = Below;
				XConfigureWindow(wm.dpy, window, CWSibling | CWStackMode, &changes);
				XFree(children);
				return;
			}
		}
		if (children) XFree(children);
	}

	// No always-on-top windows found, or XQueryTree failed.
	XRaiseWindow(wm.dpy, window);
}

int ignore_x_error(Display *dpy, XErrorEvent *err) {
	(void)dpy;
	(void)err;
	return 0;
}

int window_exists(Window window) {
	if (window == None) return 0;
	XErrorHandler old = XSetErrorHandler(ignore_x_error);
	XWindowAttributes attr;
	Status status = XGetWindowAttributes(wm.dpy, window, &attr);
	XSync(wm.dpy, False);
	XSetErrorHandler(old);
	return status != 0;
}

void set_active_window(Window window, Time time) {
	(void)time; // Use CurrentTime for more reliable focus stealing/pulling.

	if (window != None) {
		if (!window_exists(window)) return;

		wm.active = window;
		XChangeProperty(wm.dpy, wm.root, _NET_ACTIVE_WINDOW, XA_WINDOW, 32, PropModeReplace, (unsigned char *)&window, 1);

		// Check for WM_TAKE_FOCUS support.
		int take_focus = 0;
		Atom *protocols = NULL;
		int count = 0;
		if (XGetWMProtocols(wm.dpy, window, &protocols, &count)) {
			for (int i = 0; i < count; i++) {
				if (protocols[i] == WM_TAKE_FOCUS) {
					take_focus = 1;
					break;
				}
			}
			XFree(protocols);
		}

		// Check WM_HINTS if needed (logging only for now as we force focus anyway)
		XWMHints *hints = XGetWMHints(wm.dpy, window);
		if (hints) {
			log_message(stdout, LOG_DEBUG, "Window hints: input=%d", !!(hints->flags & InputHint && hints->input));
			XFree(hints);
		}

		// Always set X focus for managed windows.
		XSetInputFocus(wm.dpy, window, RevertToParent, CurrentTime);
		log_message(stdout, LOG_DEBUG, "Set input focus to 0x%lx", window);

		if (take_focus) {
			XEvent ev = {0};
			ev.type = ClientMessage;
			ev.xclient.window = window;
			ev.xclient.message_type = WM_PROTOCOLS;
			ev.xclient.format = 32;
			ev.xclient.data.l[0] = WM_TAKE_FOCUS;
			ev.xclient.data.l[1] = CurrentTime;
			XSendEvent(wm.dpy, window, False, NoEventMask, &ev);
			log_message(stdout, LOG_DEBUG, "Sent WM_TAKE_FOCUS to 0x%lx", window);
		}
	} else {
		XDeleteProperty(wm.dpy, wm.root, _NET_ACTIVE_WINDOW);
		wm.active = None;
		XSetInputFocus(wm.dpy, wm.root, RevertToParent, CurrentTime);
		log_message(stdout, LOG_DEBUG, "Reset focus to root");
	}
	XFlush(wm.dpy);
}

Window get_active_window(void) {
	Atom _NET_ACTIVE_WINDOW = XInternAtom(wm.dpy, "_NET_ACTIVE_WINDOW", False);
	Atom actual_type;
	int actual_format;
	unsigned long nitems, bytes_after;
	unsigned char *prop = NULL;
	Window active = None;

	if (XGetWindowProperty(wm.dpy, wm.root, _NET_ACTIVE_WINDOW, 0, (~0L), False, AnyPropertyType, &actual_type, &actual_format, &nitems, &bytes_after, &prop) == Success) {
		if (prop && nitems >= 1) {
			active = *(Window *)prop;
		}
	}

	if (prop) XFree(prop);
	return active;
}

void get_cursor_offset(Window window, int *dx, int *dy) {
	Window root, child;
	int root_x, root_y;
	unsigned int mask;
	XQueryPointer(wm.dpy, window, &root, &child, &root_x, &root_y, dx, dy, &mask);
}

// https://tronche.com/gui/x/xlib/events/structure-control/configure.html
void handle_configure_request(void) {
	XConfigureRequestEvent *ev = &wm.ev.xconfigurerequest;
	XWindowChanges changes;

	changes.x = ev->x;
	changes.y = ev->y;
	changes.width = ev->width;
	changes.height = ev->height;
	changes.border_width = ev->border_width;
	changes.sibling = ev->above;
	changes.stack_mode = ev->detail;

	XErrorHandler old = XSetErrorHandler(ignore_x_error);
	XConfigureWindow(wm.dpy, ev->window, ev->value_mask, &changes);
	XSync(wm.dpy, False);
	XSetErrorHandler(old);

	log_message(stdout, LOG_DEBUG, "ConfigureRequest for 0x%lx (x=%d, y=%d, w=%d, h=%d)", ev->window, ev->x, ev->y, ev->width, ev->height);
}

void handle_configure_notify(void) {
	XConfigureEvent *ev = &wm.ev.xconfigure;

	if (ev->window == wm.root || ev->send_event) return;
	
	// Only send synthetic events for windows we manage as top-level clients.
	Client *c;
	for (c = wm.clients; c; c = c->next) {
		if (c->window == ev->window) {
			log_message(stdout, LOG_DEBUG, "Sending synthetic ConfigureNotify to 0x%lx (x=%d, y=%d, w=%d, h=%d)", ev->window, ev->x, ev->y, ev->width, ev->height);
			send_configure(c->window);
			break;
		}
	}
}

void handle_map_notify(void) {
	Window window = wm.ev.xmap.window;
	if (window == wm.root) return;

	log_message(stdout, LOG_DEBUG, "MapNotify for 0x%lx", window);

	// Check if this is a managed window.
	Client *c;
	for (c = wm.clients; c; c = c->next) {
		if (c->window == window) {
			// Shows, raises and focuses the window.
			set_active_border(window);
			set_active_window(window, CurrentTime);
			
			// Ensure it has synthetic configure after mapping.
			send_configure(window);
			
			log_message(stdout, LOG_DEBUG, "Focused and configured 0x%lx after MapNotify", window);
			break;
		}
	}
	redraw_widgets();
}

// https://tronche.com/gui/x/xlib/events/structure-control/map.html
void handle_map_request(void) {
	Window window = wm.ev.xmaprequest.window;
	if (window == wm.root) return;

	XErrorHandler old = XSetErrorHandler(ignore_x_error);

	// Move window under cursor position and clamps inside the screen bounds.
	XWindowAttributes check_attr;
	if (XGetWindowAttributes(wm.dpy, window, &check_attr)) {
		XSelectInput(wm.dpy, window, EnterWindowMask | LeaveWindowMask | StructureNotifyMask | FocusChangeMask);

		Window root_return, child_return;
		int root_x, root_y, win_x, win_y;
		unsigned int mask;

		if (XQueryPointer(wm.dpy, wm.root, &root_return, &child_return, &root_x, &root_y, &win_x, &win_y, &mask)) {
			int new_x = root_x - (check_attr.width / 2);
			int new_y = root_y - (check_attr.height / 2);
			int screen_width = DisplayWidth(wm.dpy, wm.screen);
			int screen_height = DisplayHeight(wm.dpy, wm.screen);

			if (new_x < 0) new_x = 0;
			if (new_y < 0) new_y = 0;
			if (new_x + check_attr.width > screen_width) new_x = screen_width - check_attr.width;
			if (new_y + check_attr.height > screen_height) new_y = screen_height - check_attr.height;

			XMoveWindow(wm.dpy, window, new_x, new_y);
			log_message(stdout, LOG_DEBUG, "Positioned new window 0x%lx at cursor (%d, %d)", window, root_x, root_y);
		}
	}

	// Tag window with current desktop.
	unsigned long desktop = wm.current_desktop;
	XChangeProperty(wm.dpy, window, _NET_WM_DESKTOP, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&desktop, 1);

	// Grab buttons for click-to-focus.
	grab_buttons(window);

	add_client(window);
	apply_tiling_layout();

	XMapWindow(wm.dpy, window);
	raise_window(window);

	XSync(wm.dpy, False);
	XSetErrorHandler(old);

	log_message(stdout, LOG_DEBUG, "Window 0x%lx added and requested map on desktop %d", window, wm.current_desktop);
	update_client_list();
}

// https://tronche.com/gui/x/xlib/events/window-state-change/unmap.html
void handle_unmap_notify(void) {
	Window window = wm.ev.xunmap.window;
	if (window == wm.active) {
		set_active_window(None, CurrentTime);
	}

	// So if we get an unmap for a sticky window, it means the application closed it.
	if (is_sticky(window)) {
		remove_client(window);
	}

	log_message(stdout, LOG_DEBUG, "Window 0x%lx unmapped", window);
	apply_tiling_layout();
	update_client_list();
}

// https://tronche.com/gui/x/xlib/events/window-state-change/destroy.html
void handle_destroy_notify(void) {
	Window window = wm.ev.xdestroywindow.window;
	if (window == wm.active) {
		set_active_window(None, CurrentTime);
	}
	remove_client(window);
	apply_tiling_layout();
	log_message(stdout, LOG_DEBUG, "Window 0x%lx destroyed", window);
	update_client_list();
}

// https://tronche.com/gui/x/xlib/events/client-communication/property.html
void handle_property_notify(void) {
	Window window = wm.ev.xproperty.window;
	Atom prop = wm.ev.xproperty.atom;
	char *name = XGetAtomName(wm.dpy, prop);
	log_message(stdout, LOG_DEBUG, "Window 0x%lx got property notification %s", window, name);
}

// https://tronche.com/gui/x/xlib/events/keyboard-pointer/keyboard-pointer.html
void handle_motion_notify(void) {
	if (wm.start.subwindow != None && (wm.start.state & MODKEY)) {
		int xdiff = wm.ev.xmotion.x_root - wm.start.x_root;
		int ydiff = wm.ev.xmotion.y_root - wm.start.y_root;

		XMoveResizeWindow(wm.dpy, wm.start.subwindow,
				wm.attr.x + (wm.start.button == 1 ? xdiff : 0),
				wm.attr.y + (wm.start.button == 1 ? ydiff : 0),
				MAX(100, wm.attr.width  + (wm.start.button == 3 ? xdiff : 0)),
				MAX(100, wm.attr.height + (wm.start.button == 3 ? ydiff : 0)));
		send_configure(wm.start.subwindow);

		// Reset maximization state on manual move/resize.
		if (wm.start.button == 1) {
			check_and_clear_maximized_state(wm.start.subwindow, 1, 1);
		} else if (wm.start.button == 3) {
			check_and_clear_maximized_state(wm.start.subwindow, 1, 1);
		}
	}
}

// https://tronche.com/gui/x/xlib/events/client-communication/client-message.html
void handle_client_message(void) {
	Window window = wm.ev.xclient.window;
	Atom message_type = wm.ev.xclient.message_type;

	if (message_type == _NET_WM_STATE) {
		Atom atom1 = (Atom)wm.ev.xclient.data.l[1];
		Atom atom2 = (Atom)wm.ev.xclient.data.l[2];
		int action = wm.ev.xclient.data.l[0]; // 0: Remove, 1: Add, 2: Toggle

		if (atom1 == _NET_WM_STATE_FULLSCREEN || atom2 == _NET_WM_STATE_FULLSCREEN) {
			int currently_fullscreen = has_wm_state(window, _NET_WM_STATE_FULLSCREEN);
			int should_fullscreen = 0;

			if (action == 1) { // ADD
				should_fullscreen = 1;
			} else if (action == 0) { // REMOVE
				should_fullscreen = 0;
			} else if (action == 2) { // TOGGLE
				should_fullscreen = !currently_fullscreen;
			}

			set_fullscreen(window, should_fullscreen);
		}
	}

	log_message(stdout, LOG_DEBUG, "Window 0x%lx got message type of %lu", window, message_type);
	redraw_widgets();
	XFlush(wm.dpy);
}

static Client *wintoclient(Window w) {
	if (w == None || w == wm.root) return NULL;
	Client *c;
	for (c = wm.clients; c; c = c->next) {
		if (c->window == w) return c;
	}
	// Search parent hierarchy
	Window root, parent, *children;
	unsigned int nchildren;
	XErrorHandler old = XSetErrorHandler(ignore_x_error);
	while (w != wm.root && w != None) {
		if (XQueryTree(wm.dpy, w, &root, &parent, &children, &nchildren)) {
			if (children) XFree(children);
			for (c = wm.clients; c; c = c->next) {
				if (c->window == parent) {
					XSetErrorHandler(old);
					return c;
				}
			}
			w = parent;
		} else {
			break;
		}
	}
	XSetErrorHandler(old);
	return NULL;
}

static void send_configure(Window w) {
	XWindowAttributes wa;
	if (!XGetWindowAttributes(wm.dpy, w, &wa)) return;

	XConfigureEvent ce;
	ce.type = ConfigureNotify;
	ce.display = wm.dpy;
	ce.event = w;
	ce.window = w;
	ce.x = wa.x;
	ce.y = wa.y;
	ce.width = wa.width;
	ce.height = wa.height;
	ce.border_width = wa.border_width;
	ce.above = None;
	ce.override_redirect = False;
	XSendEvent(wm.dpy, w, False, StructureNotifyMask, (XEvent *)&ce);
}

static void set_fullscreen(Window window, int full) {
	int currently_fullscreen = has_wm_state(window, _NET_WM_STATE_FULLSCREEN);

	if (full && !currently_fullscreen) {
		// Enable Fullscreen
		XWindowAttributes attr;
		if (XGetWindowAttributes(wm.dpy, window, &attr)) {
			// Save geometry
			long geom[4] = { attr.x, attr.y, attr.width, attr.height };
			XChangeProperty(wm.dpy, window, _GLITCH_PRE_FULLSCREEN_GEOM, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)geom, 4);

			int screen_width = DisplayWidth(wm.dpy, wm.screen);
			int screen_height = DisplayHeight(wm.dpy, wm.screen);

			XMoveResizeWindow(wm.dpy, window, 0, 0, screen_width, screen_height);
			send_configure(window);
			XSetWindowBorderWidth(wm.dpy, window, 0);
			XRaiseWindow(wm.dpy, window);

			update_wm_state(window, _NET_WM_STATE_FULLSCREEN, 1);
			log_message(stdout, LOG_DEBUG, "Fullscreen enabled for 0x%lx", window);
		}
	} else if (!full && currently_fullscreen) {
		// Disable Fullscreen
		Atom actual_type;
		int actual_format;
		unsigned long nitems, bytes_after;
		unsigned char *prop = NULL;

		XErrorHandler old = XSetErrorHandler(ignore_x_error);
		int status = XGetWindowProperty(wm.dpy, window, _GLITCH_PRE_FULLSCREEN_GEOM, 0, 4, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
		XSync(wm.dpy, False);
		XSetErrorHandler(old);

		if (status == Success && prop && nitems == 4) {
			long *geom = (long *)prop;
			XMoveResizeWindow(wm.dpy, window, (int)geom[0], (int)geom[1], (unsigned int)geom[2], (unsigned int)geom[3]);
			send_configure(window);
		}
		if (prop) XFree(prop);

		XDeleteProperty(wm.dpy, window, _GLITCH_PRE_FULLSCREEN_GEOM);

		// Restore border
		int border_w = border_size;
		XSetWindowBorderWidth(wm.dpy, window, border_w);

		update_wm_state(window, _NET_WM_STATE_FULLSCREEN, 0);
		set_active_border(window); // Updates border color and width correctly

		log_message(stdout, LOG_DEBUG, "Fullscreen disabled for 0x%lx", window);
	}
}

void toggle_fullscreen(const Arg *arg) {
	(void)arg;
	if (wm.active == None) return;
	int currently_fullscreen = has_wm_state(wm.active, _NET_WM_STATE_FULLSCREEN);
	set_fullscreen(wm.active, !currently_fullscreen);
}

void center_window(const Arg *arg) {
	(void)arg;
	if (wm.active == None) return;

	XWindowAttributes attr;
	if (XGetWindowAttributes(wm.dpy, wm.active, &attr)) {
		int screen_width = DisplayWidth(wm.dpy, wm.screen);
		int screen_height = DisplayHeight(wm.dpy, wm.screen);
		int new_x = (screen_width - attr.width) / 2;
		int new_y = (screen_height - attr.height) / 2;

		if (new_x < 0) new_x = 0;
		if (new_y < 0) new_y = 0;

		XMoveWindow(wm.dpy, wm.active, new_x, new_y);
		send_configure(wm.active);
		log_message(stdout, LOG_DEBUG, "Centered window 0x%lx at (%d, %d)", wm.active, new_x, new_y);
	}
}

// https://tronche.com/gui/x/xlib/events/keyboard-pointer/keyboard-pointer.html
void handle_button_press(void) {
	Window window = None;
	Client *c = wintoclient(wm.ev.xbutton.window);
	if (!c) c = wintoclient(wm.ev.xbutton.subwindow);

	if (c) {
		window = c->window;
	} else if (wm.ev.xbutton.window != wm.root) {
		window = wm.ev.xbutton.window;
	} else {
		window = wm.ev.xbutton.subwindow;
	}

	log_message(stdout, LOG_DEBUG, "ButtonPress: window=0x%lx, subwindow=0x%lx, button=%d, state=0x%x, targeting=0x%lx", 
			wm.ev.xbutton.window, wm.ev.xbutton.subwindow, wm.ev.xbutton.button, wm.ev.xbutton.state, window);

	if (window == None || window == wm.root) {
		log_message(stdout, LOG_DEBUG, "ButtonPress on root or None, ignoring");
		XAllowEvents(wm.dpy, ReplayPointer, wm.ev.xbutton.time);
		return;
	}

	// Focus and raise on any click.
	set_active_border(window);
	set_active_window(window, wm.ev.xbutton.time);

	XErrorHandler old = XSetErrorHandler(ignore_x_error);
	raise_window(window);
	XSync(wm.dpy, False);
	XSetErrorHandler(old);

	log_message(stdout, LOG_DEBUG, "Focused and raised window 0x%lx", window);

	if (wm.ev.xbutton.state & MODKEY) {
		old = XSetErrorHandler(ignore_x_error);
		Status s = XGetWindowAttributes(wm.dpy, window, &wm.attr);
		XSync(wm.dpy, False);
		XSetErrorHandler(old);

		if (s == 0) {
			log_message(stdout, LOG_WARNING, "Failed to get window attributes for 0x%lx, ignoring drag", window);
			XAllowEvents(wm.dpy, AsyncPointer, CurrentTime);
			return;
		}

		wm.start = wm.ev.xbutton;
		// Ensure we use the top-level window for dragging, not a sub-window.
		wm.start.subwindow = window;

		// Set global error handler to ignore errors during drag (e.g. if window closes).
		XSetErrorHandler(ignore_x_error);

		switch (wm.ev.xbutton.button) {
			case 1: {
						XDefineCursor(wm.dpy, window, wm.cursor_move);
						log_message(stdout, LOG_DEBUG, "Setting cursor to move");
					} break;
			case 3: {
						XDefineCursor(wm.dpy, window, wm.cursor_resize);
						log_message(stdout, LOG_DEBUG, "Setting cursor to resize");
					} break;
		}

		// Use AsyncPointer to consume the event and unfreeze the pointer.
		XAllowEvents(wm.dpy, AsyncPointer, wm.ev.xbutton.time);
		log_message(stdout, LOG_DEBUG, "Window 0x%lx got button press with MODKEY, unfreezing", window);
	} else {
		// Replay the click to the application if no modifier was used.
		XAllowEvents(wm.dpy, ReplayPointer, wm.ev.xbutton.time);
		log_message(stdout, LOG_DEBUG, "Window 0x%lx got button press, replaying pointer", window);
	}

	redraw_widgets();
	XFlush(wm.dpy);
}



void goto_desktop(const Arg *arg) {
	if (arg->i < 1 || arg->i > NUM_DESKTOPS || (unsigned int)arg->i == wm.current_desktop) return;

	unsigned int old_desktop = wm.current_desktop;
	wm.current_desktop = arg->i;

	unsigned int nchildren;
	Window root_return, parent_return, *children;
	XWindowAttributes wa;
	Window new_active = None;

	if (XQueryTree(wm.dpy, wm.root, &root_return, &parent_return, &children, &nchildren)) {
		for (unsigned int i = 0; i < nchildren; i++) {
			if (!XGetWindowAttributes(wm.dpy, children[i], &wa) || wa.override_redirect)
				continue;

			unsigned long desktop;
			Atom actual_type;
			int actual_format;
			unsigned long nitems, bytes_after;
			unsigned char *prop = NULL;

			int status = XGetWindowProperty(wm.dpy, children[i], _NET_WM_DESKTOP, 0, 1, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
			if (status == Success && prop && nitems > 0) {
				desktop = *(unsigned long *)prop;
				if (desktop == (unsigned long)arg->i) {
					XMapWindow(wm.dpy, children[i]);
					new_active = children[i];
				} else if (desktop == (unsigned long)old_desktop) {
					// Don't unmap sticky windows
					if (!is_sticky(children[i])) {
						XUnmapWindow(wm.dpy, children[i]);
					}
				}
			}
			// Sticky windows should always be shown and raised
			if (is_sticky(children[i])) {
				XRaiseWindow(wm.dpy, children[i]);
			}
			if (prop) XFree(prop);
		}
		if (children) XFree(children);
	}

	unsigned long desktop_val = wm.current_desktop;
	XChangeProperty(wm.dpy, wm.root, _NET_CURRENT_DESKTOP, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&desktop_val, 1);

	set_active_window(new_active, CurrentTime);
	set_active_border(new_active);
	apply_tiling_layout();

	widget_desktop_indicator();
	widget_datetime();
	log_message(stdout, LOG_DEBUG, "Switched to desktop %d", wm.current_desktop);
	XFlush(wm.dpy);
}

void send_window_to_desktop(const Arg *arg) {
	if (wm.active == None || arg->i < 1 || arg->i > NUM_DESKTOPS || (unsigned int)arg->i == wm.current_desktop) return;

	unsigned long desktop = arg->i;
	XChangeProperty(wm.dpy, wm.active, _NET_WM_DESKTOP, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&desktop, 1);

	// Reset border before unmapping to avoid "stuck" active borders on other desktops.
	XSetWindowBorder(wm.dpy, wm.active, wm.borders.normal_inactive);
	XUnmapWindow(wm.dpy, wm.active);

	wm.active = None;
	apply_tiling_layout();
	widget_desktop_indicator();
	widget_datetime();
	log_message(stdout, LOG_DEBUG, "Moved window to desktop %d", arg->i);
	XFlush(wm.dpy);
}


void update_client_list(void) {
	XDeleteProperty(wm.dpy, wm.root, _NET_CLIENT_LIST);

	Client *c = wm.clients;
	while (c) {
		XChangeProperty(wm.dpy, wm.root, _NET_CLIENT_LIST, XA_WINDOW, 32, PropModeAppend, (unsigned char *)&c->window, 1);
		c = c->next;
	}
}

void grab_buttons(Window window) {
	XGrabButton(wm.dpy, AnyButton, AnyModifier, window, True, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
}

// https://tronche.com/gui/x/xlib/events/keyboard-pointer/keyboard-pointer.html
void handle_button_release(void) {
	if (wm.start.subwindow != None && (wm.start.state & MODKEY)) {
		XDefineCursor(wm.dpy, wm.start.subwindow, None);

		// Restore window manager error handler
		XSetErrorHandler(x_error_handler);

		log_message(stdout, LOG_DEBUG, "Restored window manager cursor on window 0x%lx", wm.start.subwindow);
		wm.start.subwindow = None;
	}

	log_message(stdout, LOG_DEBUG, "ButtonRelease: event window=0x%lx, subwindow=0x%lx", wm.ev.xbutton.window, wm.ev.xbutton.subwindow);
	XFlush(wm.dpy);
}

// https://tronche.com/gui/x/xlib/events/keyboard-pointer/keyboard-pointer.html
void handle_key_press(void) {
	if (wm.launcher_active) {
		launcher_handle_key();
		return;
	}

	log_message(stdout, LOG_DEBUG, ">> Key pressed > active window 0x%lx", wm.ev.xkey.subwindow);
	if (wm.ev.type != KeyPress) return;

	KeySym keysym = XLookupKeysym(&wm.ev.xkey, 0);

	// Check keybinds first.
	for (unsigned int i = 0; i < LENGTH(keybinds); i++) {
		if (keysym == keybinds[i].keysym && (wm.ev.xkey.state & (Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|ControlMask|ShiftMask)) == keybinds[i].mod) {
			keybinds[i].func(&keybinds[i].arg);
			XFlush(wm.dpy);
			return;
		}
	}

	// Check shortcuts next.
	for (unsigned int i = 0; i < LENGTH(shortcuts); i++) {
		if (keysym == shortcuts[i].keysym && (wm.ev.xkey.state & (Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|ControlMask|ShiftMask)) == shortcuts[i].mod) {
			execute_shortcut(shortcuts[i].cmd);
			XFlush(wm.dpy);
			return;
		}
	}

	XFlush(wm.dpy);
}

// https://tronche.com/gui/x/xlib/events/keyboard-pointer/keyboard-pointer.html
void handle_key_release(void) {
	if (wm.ev.type != KeyRelease) return;

	KeySym keysym = XLookupKeysym(&wm.ev.xkey, 0);

	if (wm.is_cycling) {
		if (keysym == XK_Alt_L || keysym == XK_Alt_R) {
			// Activate selected window
			if (wm.cycle_clients && wm.cycle_count > 0 && wm.active_cycle_index >= 0) {
				Window target = wm.cycle_clients[wm.active_cycle_index];
				set_active_border(target);
				set_active_window(target, CurrentTime);
				raise_window(target);
				XSync(wm.dpy, False);
			}
			end_cycling();
		}
	}
}

void handle_focus_in(void) {
	Window window = wm.ev.xfocus.window;
	if (window == wm.root || window == None) return;

	Client *c = wintoclient(window);
	if (wm.active != None && (!c || c->window != wm.active)) {
		log_message(stdout, LOG_DEBUG, "Window 0x%lx focus in (foreign), forcing back to active 0x%lx", window, wm.active);
		set_active_window(wm.active, CurrentTime);
	}
}

void handle_focus_out(void) {
	Window window = wm.ev.xfocus.window;
	if (window != wm.root) {
		log_message(stdout, LOG_DEBUG, "Window 0x%lx focus out", window);
	}
}

void handle_enter_notify(void) {
	Window window = wm.ev.xcrossing.window;
	if (window != wm.root) {
		log_message(stdout, LOG_DEBUG, "Window 0x%lx enter notify", window);
	}
}

void handle_expose(void) {
	if (wm.ev.xexpose.count == 0 && wm.ev.xexpose.window == wm.root) {
		// Rate limit redraws from Expose events to 200ms (5fps) to prevent flashing/high CPU
		struct timespec ts;
		clock_gettime(CLOCK_MONOTONIC, &ts);
		unsigned long now_ms = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

		if (now_ms - wm.last_widget_update > 50) {
			redraw_widgets();
			wm.last_widget_update = now_ms;
		}
	}
}

void set_active_border(Window window) {
	if (window == None) return;
	if (!window_exists(window)) return;

	// Setting current active window to inactive.
	if (wm.active != None && wm.active != window) {
		update_window_border(wm.active, 0);
		log_message(stdout, LOG_DEBUG, "Active window 0x%lx border set to inactive", wm.active);
	}

	// Setting desired window to active.
	update_window_border(window, 1);
	XFlush(wm.dpy);

	log_message(stdout, LOG_DEBUG, "Desired window 0x%lx border set to active", window);
}

void move_window_x(const Arg *arg) {
	if (wm.active == None) return;

	XWindowAttributes attr;
	XGetWindowAttributes(wm.dpy, wm.active, &attr);
	XMoveWindow(wm.dpy, wm.active, attr.x + arg->i, attr.y);
	check_and_clear_maximized_state(wm.active, 1, 0);
	log_message(stdout, LOG_DEBUG, "Move window 0x%lx on X by %d", wm.active, arg->i);

	XSync(wm.dpy, False);
	XFlush(wm.dpy);
}

void move_window_y(const Arg *arg) {
	if (wm.active == None) return;

	XWindowAttributes attr;
	XGetWindowAttributes(wm.dpy, wm.active, &attr);
	XMoveWindow(wm.dpy, wm.active, attr.x, attr.y + arg->i);
	check_and_clear_maximized_state(wm.active, 0, 1);
	log_message(stdout, LOG_DEBUG, "Move window 0x%lx on Y by %d", wm.active, arg->i);

	XSync(wm.dpy, False);
	XFlush(wm.dpy);
}

void close_window(const Arg *arg) {
	(void)arg;
	if (wm.active == None) return;

	int supported = 0;
	Atom *protocols = NULL;
	int count = 0;
	if (XGetWMProtocols(wm.dpy, wm.active, &protocols, &count)) {
		for (int i = 0; i < count; i++) {
			if (protocols[i] == WM_DELETE_WINDOW) {
				supported = 1;
				break;
			}
		}
		XFree(protocols);
	}

	if (supported) {
		XEvent ev = {0};
		ev.type = ClientMessage;
		ev.xclient.window = wm.active;
		ev.xclient.message_type = WM_PROTOCOLS;
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = WM_DELETE_WINDOW;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(wm.dpy, wm.active, False, NoEventMask, &ev);
		log_message(stdout, LOG_DEBUG, "Sent WM_DELETE_WINDOW to 0x%lx", wm.active);
	} else {
		XKillClient(wm.dpy, wm.active);
		log_message(stdout, LOG_DEBUG, "Killed client 0x%lx", wm.active);
	}
}

void resize_window_x(const Arg *arg) {
	if (wm.active == None) return;

	XWindowAttributes attr;
	XGetWindowAttributes(wm.dpy, wm.active, &attr);
	XResizeWindow(wm.dpy, wm.active, MAX(1, attr.width + arg->i), attr.height);
	check_and_clear_maximized_state(wm.active, 1, 0);
	XFlush(wm.dpy);

	log_message(stdout, LOG_DEBUG, "Resize window 0x%lx on X by %d", wm.active, arg->i);
}

void resize_window_y(const Arg *arg) {
	if (wm.active == None) return;

	XWindowAttributes attr;
	XGetWindowAttributes(wm.dpy, wm.active, &attr);
	XResizeWindow(wm.dpy, wm.active, attr.width, MAX(1, attr.height + arg->i));
	check_and_clear_maximized_state(wm.active, 0, 1);
	XFlush(wm.dpy);

	log_message(stdout, LOG_DEBUG, "Resize window 0x%lx on Y by %d", wm.active, arg->i);
}

void window_snap_up(const Arg *arg) {
	(void)arg;
	if (wm.active == None) return;

	XWindowAttributes attr;
	if (!XGetWindowAttributes(wm.dpy, wm.active, &attr)) {
		log_message(stdout, LOG_DEBUG, "Failed to get window attributes for 0x%lx", wm.active);
		return;
	}

	int rel_x, rel_y;
	get_cursor_offset(wm.active, &rel_x, &rel_y);

	XMoveWindow(wm.dpy, wm.active, attr.x, 0);
	check_and_clear_maximized_state(wm.active, 0, 1);
	XFlush(wm.dpy);

	log_message(stdout, LOG_DEBUG, "Snapped window 0x%lx to top edge", wm.active);
}

void window_snap_down(const Arg *arg) {
	(void)arg;
	if (wm.active == None) return;

	XWindowAttributes attr;
	if (!XGetWindowAttributes(wm.dpy, wm.active, &attr)) {
		log_message(stdout, LOG_DEBUG, "Failed to get window attributes for 0x%lx", wm.active);
		return;
	}

	int rel_x, rel_y;
	int y = DisplayHeight(wm.dpy, DefaultScreen(wm.dpy)) - attr.height - (2 * attr.border_width);
	get_cursor_offset(wm.active, &rel_x, &rel_y);

	XMoveWindow(wm.dpy, wm.active, attr.x, y);
	check_and_clear_maximized_state(wm.active, 0, 1);
	XFlush(wm.dpy);

	log_message(stdout, LOG_DEBUG, "Snapped window 0x%lx to bottom edge", wm.active);
}

void window_snap_left(const Arg *arg) {
	(void)arg;
	if (wm.active == None) return;

	XWindowAttributes attr;
	if (!XGetWindowAttributes(wm.dpy, wm.active, &attr)) {
		log_message(stdout, LOG_DEBUG, "Failed to get window attributes for 0x%lx", wm.active);
		return;
	}

	int rel_x, rel_y;
	get_cursor_offset(wm.active, &rel_x, &rel_y);

	XMoveWindow(wm.dpy, wm.active, 0, attr.y);
	check_and_clear_maximized_state(wm.active, 1, 0);
	XFlush(wm.dpy);

	log_message(stdout, LOG_DEBUG, "Snapped window 0x%lx to left edge", wm.active);
}

void window_snap_right(const Arg *arg) {
	(void)arg;
	if (wm.active == None) return;

	XWindowAttributes attr;
	if (!XGetWindowAttributes(wm.dpy, wm.active, &attr)) {
		log_message(stdout, LOG_DEBUG, "Failed to get window attributes for 0x%lx", wm.active);
		return;
	}

	int rel_x, rel_y;
	int x = DisplayWidth(wm.dpy, DefaultScreen(wm.dpy)) - attr.width - (2 * attr.border_width);
	get_cursor_offset(wm.active, &rel_x, &rel_y);

	XMoveWindow(wm.dpy, wm.active, x, attr.y);
	check_and_clear_maximized_state(wm.active, 1, 0);
	XFlush(wm.dpy);

	log_message(stdout, LOG_DEBUG, "Snapped window 0x%lx to right edge", wm.active);
}

static void update_wm_state(Window w, Atom state_atom, int add) {
	Atom actual_type;
	int actual_format;
	unsigned long nitems, bytes_after;
	unsigned char *prop = NULL;

	XErrorHandler old = XSetErrorHandler(ignore_x_error);
	int status = XGetWindowProperty(wm.dpy, w, _NET_WM_STATE, 0, (~0L), False, XA_ATOM, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
	XSync(wm.dpy, False);
	XSetErrorHandler(old);

	if (status == Success) {
		Atom *new_atoms = malloc(sizeof(Atom) * (nitems + 1));
		int count = 0;
		if (prop && nitems > 0) {
			Atom *atoms = (Atom *)prop;
			for (unsigned long i = 0; i < nitems; i++) {
				if (atoms[i] != state_atom) {
					new_atoms[count++] = atoms[i];
				}
			}
		}
		if (add) {
			new_atoms[count++] = state_atom;
		}
		XChangeProperty(wm.dpy, w, _NET_WM_STATE, XA_ATOM, 32, PropModeReplace, (unsigned char *)new_atoms, count);
		free(new_atoms);
	}
	if (prop) XFree(prop);
}

static int has_wm_state(Window w, Atom state_atom) {
	Atom actual_type;
	int actual_format;
	unsigned long nitems, bytes_after;
	unsigned char *prop = NULL;
	int found = 0;

	XErrorHandler old = XSetErrorHandler(ignore_x_error);
	int status = XGetWindowProperty(wm.dpy, w, _NET_WM_STATE, 0, (~0L), False, XA_ATOM, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
	XSync(wm.dpy, False);
	XSetErrorHandler(old);

	if (status == Success) {
		if (prop && nitems > 0) {
			Atom *atoms = (Atom *)prop;
			for (unsigned long i = 0; i < nitems; i++) {
				if (atoms[i] == state_atom) {
					found = 1;
					break;
				}
			}
		}
	}
	if (prop) XFree(prop);
	return found;
}

static void check_and_clear_maximized_state(Window w, int horizontal, int vertical) {
	if (horizontal && has_wm_state(w, _NET_WM_STATE_MAXIMIZED_HORZ)) {
		update_wm_state(w, _NET_WM_STATE_MAXIMIZED_HORZ, 0);
		XDeleteProperty(wm.dpy, w, _GLITCH_PRE_HMAX_GEOM);
		log_message(stdout, LOG_DEBUG, "Cleared horizontal maximization state for window 0x%lx due to interaction", w);
	}
	if (vertical && has_wm_state(w, _NET_WM_STATE_MAXIMIZED_VERT)) {
		update_wm_state(w, _NET_WM_STATE_MAXIMIZED_VERT, 0);
		XDeleteProperty(wm.dpy, w, _GLITCH_PRE_VMAX_GEOM);
		log_message(stdout, LOG_DEBUG, "Cleared vertical maximization state for window 0x%lx due to interaction", w);
	}
}

void window_hmaximize(const Arg *arg) {
	(void)arg;
	if (wm.active == None) return;

	if (has_wm_state(wm.active, _NET_WM_STATE_MAXIMIZED_HORZ)) {
		// Restore geometry
		Atom actual_type;
		int actual_format;
		unsigned long nitems, bytes_after;
		unsigned char *prop = NULL;

		XErrorHandler old = XSetErrorHandler(ignore_x_error);
		int status = XGetWindowProperty(wm.dpy, wm.active, _GLITCH_PRE_HMAX_GEOM, 0, 4, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
		XSync(wm.dpy, False);
		XSetErrorHandler(old);

		if (status == Success) {
			if (prop && nitems == 4) {
				long *geom = (long *)prop;
				XMoveResizeWindow(wm.dpy, wm.active, (int)geom[0], (int)geom[1], (unsigned int)geom[2], (unsigned int)geom[3]);
				update_wm_state(wm.active, _NET_WM_STATE_MAXIMIZED_HORZ, 0);
				XDeleteProperty(wm.dpy, wm.active, _GLITCH_PRE_HMAX_GEOM);
				log_message(stdout, LOG_DEBUG, "Restored horizontal geometry for window 0x%lx", wm.active);
			}
		}
		if (prop) XFree(prop);
	} else {
		// Save geometry and maximize
		XWindowAttributes attr;
		if (XGetWindowAttributes(wm.dpy, wm.active, &attr)) {
			long geom[4] = { attr.x, attr.y, attr.width, attr.height };
			XChangeProperty(wm.dpy, wm.active, _GLITCH_PRE_HMAX_GEOM, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)geom, 4);

			int screen_width = DisplayWidth(wm.dpy, wm.screen);
			XMoveResizeWindow(wm.dpy, wm.active, 0, attr.y, screen_width - (2 * attr.border_width), attr.height);
			update_wm_state(wm.active, _NET_WM_STATE_MAXIMIZED_HORZ, 1);
			log_message(stdout, LOG_DEBUG, "Horizontally maximized window 0x%lx", wm.active);
		}
	}
	XFlush(wm.dpy);
}

void window_vmaximize(const Arg *arg) {
	(void)arg;
	if (wm.active == None) return;

	if (has_wm_state(wm.active, _NET_WM_STATE_MAXIMIZED_VERT)) {
		// Restore geometry
		Atom actual_type;
		int actual_format;
		unsigned long nitems, bytes_after;
		unsigned char *prop = NULL;

		XErrorHandler old = XSetErrorHandler(ignore_x_error);
		int status = XGetWindowProperty(wm.dpy, wm.active, _GLITCH_PRE_VMAX_GEOM, 0, 4, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
		XSync(wm.dpy, False);
		XSetErrorHandler(old);

		if (status == Success) {
			if (prop && nitems == 4) {
				long *geom = (long *)prop;
				XMoveResizeWindow(wm.dpy, wm.active, (int)geom[0], (int)geom[1], (unsigned int)geom[2], (unsigned int)geom[3]);
				update_wm_state(wm.active, _NET_WM_STATE_MAXIMIZED_VERT, 0);
				XDeleteProperty(wm.dpy, wm.active, _GLITCH_PRE_VMAX_GEOM);
				log_message(stdout, LOG_DEBUG, "Restored vertical geometry for window 0x%lx", wm.active);
			}
		}
		if (prop) XFree(prop);
	} else {
		// Save geometry and maximize
		XWindowAttributes attr;
		if (XGetWindowAttributes(wm.dpy, wm.active, &attr)) {
			long geom[4] = { attr.x, attr.y, attr.width, attr.height };
			XChangeProperty(wm.dpy, wm.active, _GLITCH_PRE_VMAX_GEOM, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)geom, 4);

			int screen_height = DisplayHeight(wm.dpy, wm.screen);
			XMoveResizeWindow(wm.dpy, wm.active, attr.x, 0, attr.width, screen_height - (2 * attr.border_width));
			update_wm_state(wm.active, _NET_WM_STATE_MAXIMIZED_VERT, 1);
			log_message(stdout, LOG_DEBUG, "Vertically maximized window 0x%lx", wm.active);
		}
	}
	XFlush(wm.dpy);
}

void quit(const Arg *arg) {
	(void)arg;
	log_message(stdout, LOG_DEBUG, "Quit window manager");
	wm.running = 0;
}

void toggle_pip(const Arg *arg) {
	(void)arg;
	if (wm.active == None) return;

	int sticky = is_sticky(wm.active);
	unsigned long desktop = sticky ? wm.current_desktop : 0xFFFFFFFF; // 0xFFFFFFFF is often used for "all desktops"

	// Toggle _NET_WM_DESKTOP
	XChangeProperty(wm.dpy, wm.active, _NET_WM_DESKTOP, XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&desktop, 1);

	// Toggle _NET_WM_STATE_STICKY
	if (!sticky) {
		XChangeProperty(wm.dpy, wm.active, _NET_WM_STATE, XA_ATOM, 32, PropModeAppend, (unsigned char *)&_NET_WM_STATE_STICKY, 1);

		// If enabling PiP: resize to a small corner window
		int screen_width = DisplayWidth(wm.dpy, wm.screen);
		int screen_height = DisplayHeight(wm.dpy, wm.screen);
		int pip_w = screen_width / 4;
		int pip_h = screen_height / 4;
		int x = screen_width - pip_w - 20;
		int y = screen_height - pip_h - 20;

		XMoveResizeWindow(wm.dpy, wm.active, x, y, pip_w, pip_h);
		XRaiseWindow(wm.dpy, wm.active);
	} else {
		// If disabling: just remove sticky state (could restore size if we saved it)
		// For now, let's just remove the atom. This is a bit complex with XChangeProperty, 
		// but since we only have one state usually it might be okay to just clear it if we were sure.
		// Better way: get property, remove atom from list, set property.

		Atom actual_type;
		int actual_format;
		unsigned long nitems, bytes_after;
		unsigned char *prop = NULL;

		XErrorHandler old = XSetErrorHandler(ignore_x_error);
		int status = XGetWindowProperty(wm.dpy, wm.active, _NET_WM_STATE, 0, (~0L), False, XA_ATOM, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
		XSync(wm.dpy, False);
		XSetErrorHandler(old);

		if (status == Success) {
			if (prop && nitems > 0) {
				Atom *atoms = (Atom *)prop;
				Atom *new_atoms = malloc(sizeof(Atom) * nitems);
				int count = 0;
				for (unsigned long i = 0; i < nitems; i++) {
					if (atoms[i] != _NET_WM_STATE_STICKY) {
						new_atoms[count++] = atoms[i];
					}
				}
				XChangeProperty(wm.dpy, wm.active, _NET_WM_STATE, XA_ATOM, 32, PropModeReplace, (unsigned char *)new_atoms, count);
				free(new_atoms);
			}
		}
		if (prop) XFree(prop);
	}

	set_active_border(wm.active);
	widget_desktop_indicator();
	widget_datetime();
	log_message(stdout, LOG_DEBUG, "Toggled PiP for window 0x%lx (sticky=%d)", wm.active, !sticky);
	XFlush(wm.dpy);
}

int is_sticky(Window window) {
	if (window == None) return 0;

	// Check _NET_WM_DESKTOP first
	Atom actual_type;
	int actual_format;
	unsigned long nitems, bytes_after;
	unsigned char *prop = NULL;

	XErrorHandler old = XSetErrorHandler(ignore_x_error);
	int status = XGetWindowProperty(wm.dpy, window, _NET_WM_DESKTOP, 0, 1, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
	XSync(wm.dpy, False);
	XSetErrorHandler(old);

	if (status == Success) {
		if (prop && nitems > 0) {
			unsigned long desktop = *(unsigned long *)prop;
			if (desktop == 0xFFFFFFFF) {
				XFree(prop);
				return 1;
			}
		}
	}
	if (prop) XFree(prop);

	// Also check _NET_WM_STATE for _NET_WM_STATE_STICKY
	prop = NULL;
	old = XSetErrorHandler(ignore_x_error);
	status = XGetWindowProperty(wm.dpy, window, _NET_WM_STATE, 0, (~0L), False, XA_ATOM, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
	XSync(wm.dpy, False);
	XSetErrorHandler(old);

	if (status == Success) {
		if (prop && nitems > 0) {
			Atom *atoms = (Atom *)prop;
			for (unsigned long i = 0; i < nitems; i++) {
				if (atoms[i] == _NET_WM_STATE_STICKY) {
					XFree(prop);
					return 1;
				}
			}
		}
	}
	if (prop) XFree(prop);

	return 0;
}

void toggle_always_on_top(const Arg *arg) {
	(void)arg;
	if (wm.active == None) return;

	int above = is_always_on_top(wm.active);
	update_wm_state(wm.active, _NET_WM_STATE_ABOVE, !above);

	raise_window(wm.active);
	set_active_border(wm.active);

	log_message(stdout, LOG_DEBUG, "Toggled always-on-top for window 0x%lx (now %s)", wm.active, !above ? "ON" : "OFF");
}

void reload(const Arg *arg) {
	(void)arg;
	wm.running = 0;
	wm.restart = 1;
	log_message(stdout, LOG_DEBUG, "Reload window manager");
}

void apply_tiling_layout(void) {
	if (wm.layout_modes[wm.current_desktop] != LAYOUT_TILING) return;

	int n = 0;
	for (Client *c = wm.clients; c; c = c->next) {
		if (window_exists(c->window)) {
			unsigned long desktop;
			Atom actual_type;
			int actual_format;
			unsigned long nitems, bytes_after;
			unsigned char *prop = NULL;

			int status = XGetWindowProperty(wm.dpy, c->window, _NET_WM_DESKTOP, 0, 1, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
			if (status == Success && prop && nitems > 0) {
				desktop = *(unsigned long *)prop;
				XFree(prop);
				if (desktop == wm.current_desktop && !is_sticky(c->window) && !is_always_on_top(c->window) && !has_wm_state(c->window, _NET_WM_STATE_FULLSCREEN)) {
					n++;
				}
			} else if (prop) {
				XFree(prop);
			}
		}
	}

	if (n == 0) return;

	int screen_width = DisplayWidth(wm.dpy, wm.screen);
	int screen_height = DisplayHeight(wm.dpy, wm.screen);
	int mw = (n > 1) ? screen_width / 3 : screen_width;
	int i = 0;

	for (Client *c = wm.clients; c; c = c->next) {
		if (window_exists(c->window)) {
			unsigned long desktop;
			Atom actual_type;
			int actual_format;
			unsigned long nitems, bytes_after;
			unsigned char *prop = NULL;

			int status = XGetWindowProperty(wm.dpy, c->window, _NET_WM_DESKTOP, 0, 1, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
			if (status == Success && prop && nitems > 0) {
				desktop = *(unsigned long *)prop;
				XFree(prop);
				if (desktop == wm.current_desktop && !is_sticky(c->window) && !is_always_on_top(c->window) && !has_wm_state(c->window, _NET_WM_STATE_FULLSCREEN)) {
					if (n == 1) {
						XMoveResizeWindow(wm.dpy, c->window, 0, 0, screen_width - 2 * border_size, screen_height - 2 * border_size);
						send_configure(c->window);
					} else if (i == 0) { // Master
						XMoveResizeWindow(wm.dpy, c->window, 0, 0, mw - 2 * border_size, screen_height - 2 * border_size);
						send_configure(c->window);
					} else { // Stack
						int h = screen_height / (n - 1);
						XMoveResizeWindow(wm.dpy, c->window, mw, (i - 1) * h, screen_width - mw - 2 * border_size, h - 2 * border_size);
						send_configure(c->window);
					}
					i++;
				}
			} else if (prop) {
				XFree(prop);
			}
		}
	}
}

void toggle_layout(const Arg *arg) {
	(void)arg;
	LayoutMode current_mode = wm.layout_modes[wm.current_desktop];

	if (current_mode == LAYOUT_FLOATING) {
		// Moving to TILING, save floating positions
		for (Client *c = wm.clients; c; c = c->next) {
			if (!window_exists(c->window)) continue;

			unsigned long desktop;
			Atom actual_type;
			int actual_format;
			unsigned long nitems, bytes_after;
			unsigned char *prop = NULL;

			int status = XGetWindowProperty(wm.dpy, c->window, _NET_WM_DESKTOP, 0, 1, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
			if (status == Success && prop && nitems > 0) {
				desktop = *(unsigned long *)prop;
				XFree(prop);
				if (desktop == wm.current_desktop && !is_sticky(c->window) && !is_always_on_top(c->window) && !has_wm_state(c->window, _NET_WM_STATE_FULLSCREEN)) {
					XWindowAttributes attr;
					if (XGetWindowAttributes(wm.dpy, c->window, &attr)) {
						c->saved_x = attr.x;
						c->saved_y = attr.y;
						c->saved_w = attr.width;
						c->saved_h = attr.height;
						c->has_saved_state = 1;
					}
				}
			} else if (prop) {
				XFree(prop);
			}
		}
		wm.layout_modes[wm.current_desktop] = LAYOUT_TILING;
		apply_tiling_layout();
	} else {
		// Moving to FLOATING, restore positions
		wm.layout_modes[wm.current_desktop] = LAYOUT_FLOATING;
		for (Client *c = wm.clients; c; c = c->next) {
			if (!window_exists(c->window)) continue;
			if (c->has_saved_state) {
				unsigned long desktop;
				Atom actual_type;
				int actual_format;
				unsigned long nitems, bytes_after;
				unsigned char *prop = NULL;

				int status = XGetWindowProperty(wm.dpy, c->window, _NET_WM_DESKTOP, 0, 1, False, XA_CARDINAL, &actual_type, &actual_format, &nitems, &bytes_after, &prop);
				if (status == Success && prop && nitems > 0) {
					desktop = *(unsigned long *)prop;
					XFree(prop);
					if (desktop == wm.current_desktop) {
						XMoveResizeWindow(wm.dpy, c->window, c->saved_x, c->saved_y, c->saved_w, c->saved_h);
						c->has_saved_state = 0;
					}
				} else if (prop) {
					XFree(prop);
				}
			}
		}
	}
	redraw_widgets();
	log_message(stdout, LOG_DEBUG, "Toggled layout for desktop %d to %s", wm.current_desktop, wm.layout_modes[wm.current_desktop] == LAYOUT_TILING ? "TILING" : "FLOATING");
}
