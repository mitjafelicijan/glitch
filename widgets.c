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

void widget_mic_indicator(void) {
	int screen_width = DisplayWidth(wm.dpy, wm.screen);
	int padding = 3;

	// Desktop indicator size
	char desktop_buf[8];
	snprintf(desktop_buf, sizeof(desktop_buf), "%u", wm.current_desktop);
	XGlyphInfo desktop_extents;
	XftTextExtentsUtf8(wm.dpy, wm.font, (FcChar8 *)desktop_buf, strlen(desktop_buf), &desktop_extents);
	int desktop_size = (wm.font->height > desktop_extents.width ? wm.font->height : desktop_extents.width) + padding * 2;

	// Layout indicator size
	LayoutMode mode = wm.layout_modes[wm.current_desktop];
	const char *layout_buf = (mode == LAYOUT_TILING) ? "T" : "F";
	XGlyphInfo layout_extents;
	XftTextExtentsUtf8(wm.dpy, wm.font, (FcChar8 *)layout_buf, strlen(layout_buf), &layout_extents);
	int layout_size_w = (wm.font->height > layout_extents.width ? wm.font->height : layout_extents.width) + padding * 2;

	const char *buf = "MIC";
	XGlyphInfo extents;
	XftTextExtentsUtf8(wm.dpy, wm.font, (FcChar8 *)buf, strlen(buf), &extents);

	int size_w = extents.width + padding * 4;
	int size_h = desktop_size;
	int x = screen_width - desktop_size - layout_size_w - size_w - 20;
	int y = 10;

	XftColor *bg = wm.mic_muted ? &wm.xft_mic_muted_bg : &wm.xft_mic_active_bg;
	XftColor *fg = wm.mic_muted ? &wm.xft_mic_muted_fg : &wm.xft_mic_active_fg;

	// Draw the background.
	XftDrawRect(wm.xft_draw, bg, x, y, size_w, size_h);

	// Center the text.
	int text_x = x + (size_w - extents.width) / 2 + extents.x;
	int text_y = y + (size_h - wm.font->ascent - wm.font->descent) / 2 + wm.font->ascent;

	XftDrawStringUtf8(wm.xft_draw, fg, wm.font, text_x, text_y, (FcChar8 *)buf, strlen(buf));
}

void widget_layout_indicator(void) {
	int screen_width = DisplayWidth(wm.dpy, wm.screen);
	int padding = 3;

	// Desktop indicator size
	char desktop_buf[8];
	snprintf(desktop_buf, sizeof(desktop_buf), "%u", wm.current_desktop);
	XGlyphInfo desktop_extents;
	XftTextExtentsUtf8(wm.dpy, wm.font, (FcChar8 *)desktop_buf, strlen(desktop_buf), &desktop_extents);
	int desktop_size = (wm.font->height > desktop_extents.width ? wm.font->height : desktop_extents.width) + padding * 2;

	LayoutMode mode = wm.layout_modes[wm.current_desktop];
	const char *buf = (mode == LAYOUT_TILING) ? "T" : "F";
	XGlyphInfo extents;
	XftTextExtentsUtf8(wm.dpy, wm.font, (FcChar8 *)buf, strlen(buf), &extents);

	int size_w = (wm.font->height > extents.width ? wm.font->height : extents.width) + padding * 2;
	int size_h = desktop_size;
	int x = screen_width - desktop_size - size_w - 15;
	int y = 10;

	XftColor *bg = (mode == LAYOUT_TILING) ? &wm.xft_layout_tile_bg : &wm.xft_layout_float_bg;
	XftColor *fg = (mode == LAYOUT_TILING) ? &wm.xft_layout_tile_fg : &wm.xft_layout_float_fg;

	// Draw the background.
	XftDrawRect(wm.xft_draw, bg, x, y, size_w, size_h);

	// Center the text.
	int text_x = x + (size_w - extents.width) / 2 + extents.x;
	int text_y = y + (size_h - wm.font->ascent - wm.font->descent) / 2 + wm.font->ascent;

	XftDrawStringUtf8(wm.xft_draw, fg, wm.font, text_x, text_y, (FcChar8 *)buf, strlen(buf));
}

void widget_datetime(void) {
	int screen_width = DisplayWidth(wm.dpy, wm.screen);
	int padding = 3;

	// Desktop indicator size
	char desktop_buf[8];
	snprintf(desktop_buf, sizeof(desktop_buf), "%u", wm.current_desktop);
	XGlyphInfo desktop_extents;
	XftTextExtentsUtf8(wm.dpy, wm.font, (FcChar8 *)desktop_buf, strlen(desktop_buf), &desktop_extents);
	int desktop_size = (wm.font->height > desktop_extents.width ? wm.font->height : desktop_extents.width) + padding * 2;

	// Layout indicator size
	LayoutMode mode = wm.layout_modes[wm.current_desktop];
	const char *layout_buf = (mode == LAYOUT_TILING) ? "T" : "F";
	XGlyphInfo layout_extents;
	XftTextExtentsUtf8(wm.dpy, wm.font, (FcChar8 *)layout_buf, strlen(layout_buf), &layout_extents);
	int layout_size_w = (wm.font->height > layout_extents.width ? wm.font->height : layout_extents.width) + padding * 2;

	// Mic indicator size
	const char *mic_buf = "MIC";
	XGlyphInfo mic_extents;
	XftTextExtentsUtf8(wm.dpy, wm.font, (FcChar8 *)mic_buf, strlen(mic_buf), &mic_extents);
	int mic_size_w = mic_extents.width + padding * 4;

	int offset_x = desktop_size + layout_size_w + mic_size_w + 35;

	char time_buf[64];
	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);
	strftime(time_buf, sizeof(time_buf), time_format, tm_info);

	XGlyphInfo time_extents;
	XftTextExtentsUtf8(wm.dpy, wm.font, (FcChar8 *)time_buf, strlen(time_buf), &time_extents);

	int time_x = screen_width - offset_x - time_extents.xOff;
	int y = 10;
	int win_height = desktop_size;

	// Draw the background.
	XftDrawRect(wm.xft_draw, &wm.xft_root_bg_color, time_x - 50, y, time_extents.xOff + 50, win_height);

	// Draw the time.
	int time_text_y = y + (win_height - wm.font->ascent - wm.font->descent) / 2 + wm.font->ascent;
	XftDrawStringUtf8(wm.xft_draw, &wm.xft_color, wm.font, time_x, time_text_y, (FcChar8 *)time_buf, strlen(time_buf));
}

void redraw_widgets(void) {
	widget_desktop_indicator();
	widget_layout_indicator();
	widget_mic_indicator();
	widget_datetime();
}
