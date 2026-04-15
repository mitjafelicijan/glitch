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

#include "glitch.h"
#include "config.h"

extern WindowManager wm;

static void launcher_filter(void);

static char *trim_whitespace(char *str) {
	char *end;
	while (isspace((unsigned char)*str)) str++;
	if (*str == 0) return str;
	end = str + strlen(str) - 1;
	while (end > str && isspace((unsigned char)*end)) end--;
	end[1] = '\0';
	return str;
}

static void load_applications(void) {
	const char *system_dirs[] = {
		"/usr/share/applications",
		"/usr/local/share/applications"
	};

	char home_dir[1024];
	char *home = getenv("HOME");
	if (home) {
		snprintf(home_dir, sizeof(home_dir), "%s/.local/share/applications", home);
	}

	if (wm.launcher_items) {
		for (int i = 0; i < wm.launcher_items_count; i++) {
			free(wm.launcher_items[i].name);
			free(wm.launcher_items[i].exec);
		}
		free(wm.launcher_items);
		wm.launcher_items = NULL;
		wm.launcher_items_count = 0;
	}

	int capacity = 100;
	wm.launcher_items = malloc(sizeof(LauncherItem) * capacity);

	for (int d = -1; d < (int)LENGTH(system_dirs); d++) {
		const char *path_to_open;
		if (d == -1) {
			if (!home) continue;
			path_to_open = home_dir;
		} else {
			path_to_open = system_dirs[d];
		}

		DIR *dir = opendir(path_to_open);
		if (!dir) continue;

		struct dirent *entry;
		while ((entry = readdir(dir))) {
			if (strstr(entry->d_name, ".desktop")) {
				char desktop_path[2048];
				snprintf(desktop_path, sizeof(desktop_path), "%s/%s", path_to_open, entry->d_name);

				FILE *f = fopen(desktop_path, "r");
				if (!f) continue;

				char line[1024];
				char *name = NULL;
				char *exec = NULL;
				int no_display = 0;
				int in_desktop_entry = 0;

				while (fgets(line, sizeof(line), f)) {
					char *trimmed = trim_whitespace(line);
					if (trimmed[0] == '[' && strstr(trimmed, "[Desktop Entry]")) {
						in_desktop_entry = 1;
						continue;
					}
					if (trimmed[0] == '[' && !strstr(trimmed, "[Desktop Entry]")) {
						in_desktop_entry = 0;
					}

					if (!in_desktop_entry) continue;

					if (strncmp(trimmed, "Name=", 5) == 0 && !name) {
						name = strdup(trim_whitespace(trimmed + 5));
					} else if (strncmp(trimmed, "Exec=", 5) == 0 && !exec) {
						char *e = strdup(trimmed + 5);
						char *percent = strchr(e, '%');
						if (percent) *percent = '\0';
						exec = strdup(trim_whitespace(e));
						free(e);
					} else if (strncmp(trimmed, "NoDisplay=true", 14) == 0) {
						no_display = 1;
					}
				}
				fclose(f);

				if (name && exec && !no_display) {
					if (wm.launcher_items_count >= capacity) {
						capacity *= 2;
						wm.launcher_items = realloc(wm.launcher_items, sizeof(LauncherItem) * capacity);
					}
					wm.launcher_items[wm.launcher_items_count].name = name;
					wm.launcher_items[wm.launcher_items_count].exec = exec;
					wm.launcher_items_count++;
				} else {
					if (name) free(name);
					if (exec) free(exec);
				}
			}
		}
		closedir(dir);
	}
}

void toggle_launcher(const Arg *arg) {
	(void)arg;
	if (wm.launcher_active) {
		wm.launcher_active = 0;
		XUnmapWindow(wm.dpy, wm.launcher_win);
		XUngrabKeyboard(wm.dpy, CurrentTime);
		return;
	}

	if (!wm.launcher_items) {
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
