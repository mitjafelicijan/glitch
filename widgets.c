#include <time.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xft/Xft.h>

#include "glitch.h"
#include "config.h"

extern WindowManager wm;

void widget_desktop_indicator(void) {
	int screen_width = DisplayWidth(wm.dpy, wm.screen);
	int padding = 3;

	char buf[8];
	snprintf(buf, sizeof(buf), "%u", wm.current_desktop);

	XGlyphInfo extents;
	XftTextExtentsUtf8(wm.dpy, wm.font, (FcChar8 *)buf, strlen(buf), &extents);

	int size = (wm.font->height > extents.width ? wm.font->height : extents.width) + padding * 2;
	int x = screen_width - size - 10;
	int y = 10;

	// Draw the background square.
	XftDrawRect(wm.xft_draw, &wm.xft_bg_color, x, y, size, size);

	// Center the text in the square.
	int text_x = x + (size - extents.width) / 2 + extents.x;
	int text_y = y + (size - wm.font->ascent - wm.font->descent) / 2 + wm.font->ascent;

	XftDrawStringUtf8(wm.xft_draw, &wm.xft_color, wm.font, text_x, text_y, (FcChar8 *)buf, strlen(buf));
}

void widget_datetime(void) {
	int screen_width = DisplayWidth(wm.dpy, wm.screen);
	int padding = 3;

	// We need to know the desktop indicator size to position the time correctly.
	char desktop_buf[8];
	snprintf(desktop_buf, sizeof(desktop_buf), "%u", wm.current_desktop);
	XGlyphInfo desktop_extents;
	XftTextExtentsUtf8(wm.dpy, wm.font, (FcChar8 *)desktop_buf, strlen(desktop_buf), &desktop_extents);
	int desktop_size = (wm.font->height > desktop_extents.width ? wm.font->height : desktop_extents.width) + padding * 2;
	int desktop_x = screen_width - desktop_size - 10;

	char time_buf[64];
	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);
	strftime(time_buf, sizeof(time_buf), time_format, tm_info);

	XGlyphInfo time_extents;
	XftTextExtentsUtf8(wm.dpy, wm.font, (FcChar8 *)time_buf, strlen(time_buf), &time_extents);

	int time_x = desktop_x - time_extents.xOff - 20;
	int y = 10;
	int win_height = desktop_size;

	// Draw the background.
	XftDrawRect(wm.xft_draw, &wm.xft_root_bg_color, time_x - 50, y, time_extents.xOff + 50, win_height);

	// Draw the time.
	int time_text_y = y + (win_height - wm.font->ascent - wm.font->descent) / 2 + wm.font->ascent;
	XftDrawStringUtf8(wm.xft_draw, &wm.xft_color, wm.font, time_x, time_text_y, (FcChar8 *)time_buf, strlen(time_buf));
}
