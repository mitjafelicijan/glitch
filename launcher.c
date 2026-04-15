#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <sys/stat.h>
#include <gio/gio.h>

#include "glitch.h"
#include "config.h"

extern WindowManager wm;

static void launcher_filter(void);

static int compare_launcher_items(const void *a, const void *b) {
	const LauncherItem *ia = (const LauncherItem *)a;
	const LauncherItem *ib = (const LauncherItem *)b;
	if (ib->usage != ia->usage)
		return ib->usage - ia->usage;
	return strcasecmp(ia->name, ib->name);
}

static void load_usage(void) {
	char path[1024];
	char *home = getenv("HOME");
	if (!home) return;
	snprintf(path, sizeof(path), "%s/.cache/glitch/usage.db", home);

	GKeyFile *kf = g_key_file_new();
	if (g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL)) {
		for (int i = 0; i < wm.launcher_items_count; i++) {
			wm.launcher_items[i].usage = g_key_file_get_integer(kf, "Usage", wm.launcher_items[i].exec, NULL);
		}
	}
	g_key_file_free(kf);
}

static void record_usage(const char *exec) {
	char path[1024];
	char *home = getenv("HOME");
	if (!home) return;
	snprintf(path, sizeof(path), "%s/.cache/glitch", home);
	mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/.cache/glitch/usage.db", home);

	GKeyFile *kf = g_key_file_new();
	g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL);

	int count = g_key_file_get_integer(kf, "Usage", exec, NULL);
	g_key_file_set_integer(kf, "Usage", exec, count + 1);

	g_key_file_save_to_file(kf, path, NULL);
	g_key_file_free(kf);
}

static void load_applications(void) {
	if (wm.launcher_items) {
		for (int i = 0; i < wm.launcher_items_count; i++) {
			free(wm.launcher_items[i].name);
			free(wm.launcher_items[i].exec);
		}
		free(wm.launcher_items);
		wm.launcher_items = NULL;
		wm.launcher_items_count = 0;
	}

	GList *apps = g_app_info_get_all();
	int total_apps = g_list_length(apps);
	wm.launcher_items = malloc(sizeof(LauncherItem) * total_apps);

	for (GList *l = apps; l != NULL; l = l->next) {
		GAppInfo *app = (GAppInfo *)l->data;

		if (!g_app_info_should_show(app)) {
			g_object_unref(app);
			continue;
		}

		const char *name = g_app_info_get_name(app);
		const char *exec = g_app_info_get_commandline(app);

		if (name && exec) {
			wm.launcher_items[wm.launcher_items_count].name = strdup(name);
			wm.launcher_items[wm.launcher_items_count].usage = 0;

			char *e = strdup(exec);

			char *percent = strchr(e, '%');
			if (percent) *percent = '\0';

			// Trim potential trailing space after removing %
			int len = strlen(e);
			while (len > 0 && isspace((unsigned char)e[len-1])) {
				e[--len] = '\0';
			}

			wm.launcher_items[wm.launcher_items_count].exec = strdup(e);
			free(e);

			wm.launcher_items_count++;
		}
		g_object_unref(app);
	}
	g_list_free(apps);

	load_usage();
	qsort(wm.launcher_items, wm.launcher_items_count, sizeof(LauncherItem), compare_launcher_items);
}

void toggle_launcher(const Arg *arg) {
	(void)arg;
	if (wm.launcher_active) {
		wm.launcher_active = 0;
		XUnmapWindow(wm.dpy, wm.launcher_win);
		XUngrabKeyboard(wm.dpy, CurrentTime);
		return;
	}

	if (wm.launcher_items) {
		load_usage();
		qsort(wm.launcher_items, wm.launcher_items_count, sizeof(LauncherItem), compare_launcher_items);
	} else {
		load_applications();
	}

	wm.launcher_active = 1;
	wm.launcher_search[0] = '\0';
	wm.launcher_selected = 0;
	launcher_filter();

	int screen_width = DisplayWidth(wm.dpy, wm.screen);
	int screen_height = DisplayHeight(wm.dpy, wm.screen);
	int win_width = launcher_width;
	int win_height = launcher_height;
	int x = (screen_width - win_width) / 2;
	int y = (screen_height - win_height) / 2;

	if (!wm.launcher_win) {
		XSetWindowAttributes wa;
		wa.override_redirect = True;
		wa.background_pixel = wm.xft_launcher_bg.pixel;
		wa.border_pixel = wm.xft_launcher_border.pixel;
		wm.launcher_win = XCreateWindow(wm.dpy, wm.root, x, y, win_width, win_height, 2,
				DefaultDepth(wm.dpy, wm.screen), CopyFromParent,
				DefaultVisual(wm.dpy, wm.screen),
				CWOverrideRedirect | CWBackPixel | CWBorderPixel, &wa);
	} else {
		XMoveWindow(wm.dpy, wm.launcher_win, x, y);
	}

	XMapRaised(wm.dpy, wm.launcher_win);
	XGrabKeyboard(wm.dpy, wm.launcher_win, True, GrabModeAsync, GrabModeAsync, CurrentTime);
	launcher_draw();
}

static void launcher_filter(void) {
	if (wm.launcher_filtered) free(wm.launcher_filtered);
	wm.launcher_filtered = malloc(sizeof(LauncherItem *) * wm.launcher_items_count);
	wm.launcher_filtered_count = 0;

	for (int i = 0; i < wm.launcher_items_count; i++) {
		if (wm.launcher_search[0] == '\0' || 
				strcasestr(wm.launcher_items[i].name, wm.launcher_search)) {
			wm.launcher_filtered[wm.launcher_filtered_count++] = &wm.launcher_items[i];
		}
	}

	if (wm.launcher_selected >= wm.launcher_filtered_count) {
		wm.launcher_selected = wm.launcher_filtered_count > 0 ? wm.launcher_filtered_count - 1 : 0;
	}
}

void launcher_handle_key(void) {
	KeySym keysym = XLookupKeysym(&wm.ev.xkey, 0);
	int len = strlen(wm.launcher_search);

	if (keysym == XK_Escape) {
		toggle_launcher(NULL);
		return;
	} else if (keysym == XK_BackSpace) {
		if (len > 0) {
			wm.launcher_search[len - 1] = '\0';
			wm.launcher_selected = 0;
			launcher_filter();
		}
	} else if (keysym == XK_Return) {
		if (wm.launcher_filtered_count > 0 && wm.launcher_selected < wm.launcher_filtered_count) {
			record_usage(wm.launcher_filtered[wm.launcher_selected]->exec);
			execute_shortcut(wm.launcher_filtered[wm.launcher_selected]->exec);
			toggle_launcher(NULL);
			return;
		}
	} else if (keysym == XK_Up) {
		if (wm.launcher_selected > 0) wm.launcher_selected--;
	} else if (keysym == XK_Down) {
		if (wm.launcher_selected < wm.launcher_filtered_count - 1) wm.launcher_selected++;
	} else {
		char buf[32];
		int n = XLookupString(&wm.ev.xkey, buf, sizeof(buf), NULL, NULL);
		if (n > 0 && len + n < (int)sizeof(wm.launcher_search)) {
			memcpy(wm.launcher_search + len, buf, n);
			wm.launcher_search[len + n] = '\0';
			wm.launcher_selected = 0;
			launcher_filter();
		}
	}

	launcher_draw();
}

void launcher_draw(void) {
	if (!wm.launcher_win) return;

	XftDraw *draw = XftDrawCreate(wm.dpy, wm.launcher_win, DefaultVisual(wm.dpy, wm.screen), wm.cmap);
	XWindowAttributes wa;
	XGetWindowAttributes(wm.dpy, wm.launcher_win, &wa);

	// Clear background
	XftDrawRect(draw, &wm.xft_launcher_bg, 0, 0, wa.width, wa.height);

	int x = 20;
	int y = 30;
	int row_height = wm.launcher_font->height + 10;

	// Draw search bar
	char search_display[300];
	snprintf(search_display, sizeof(search_display), "> %s", wm.launcher_search);
	XftDrawStringUtf8(draw, &wm.xft_launcher_fg, wm.launcher_font, x, y, (FcChar8 *)search_display, strlen(search_display));

	y += row_height + 10; // Extra padding below input

	// Draw items
	int start_idx = 0;
	if (wm.launcher_selected >= 10) {
		start_idx = wm.launcher_selected - 9;
	}

	for (int i = start_idx; i < wm.launcher_filtered_count && i < start_idx + 15; i++) {
		if (i == wm.launcher_selected) {
			XftDrawRect(draw, &wm.xft_launcher_hl_bg, 0, y - wm.launcher_font->ascent - 5, wa.width, row_height);
			XftDrawStringUtf8(draw, &wm.xft_launcher_hl_fg, wm.launcher_font, x, y, (FcChar8 *)wm.launcher_filtered[i]->name, strlen(wm.launcher_filtered[i]->name));
		} else {
			XftDrawStringUtf8(draw, &wm.xft_launcher_fg, wm.launcher_font, x, y, (FcChar8 *)wm.launcher_filtered[i]->name, strlen(wm.launcher_filtered[i]->name));
		}
		y += row_height;
	}

	XftDrawDestroy(draw);
	XFlush(wm.dpy);
}
