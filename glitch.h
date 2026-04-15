#ifndef GLITCH_H
#define GLITCH_H

#include <stdio.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/XF86keysym.h>
#include <X11/Xft/Xft.h>
#include <pulse/pulseaudio.h>

#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#define LENGTH(x) (sizeof(x) / sizeof((x)[0]))

#define NUM_DESKTOPS 9

extern Atom _NET_WM_DESKTOP;

#define COLOR_INFO     "\x1B[0m"   // White
#define COLOR_DEBUG    "\x1B[36m"  // Cyan
#define COLOR_WARNING  "\x1B[33m"  // Yellow
#define COLOR_ERROR    "\x1B[31m"  // Red
#define COLOR_RESET    "\x1B[0m"

typedef enum {
	LOG_INFO,
	LOG_DEBUG,
	LOG_WARNING,
	LOG_ERROR,
} LogLevel;

typedef enum {
	LAYOUT_FLOATING,
	LAYOUT_TILING,
} LayoutMode;

typedef struct {
	unsigned long normal_active;
	unsigned long normal_inactive;
	unsigned long sticky_active;
	unsigned long sticky_inactive;
	unsigned long on_top_active;
	unsigned long on_top_inactive;
} Borders;

typedef struct Client {
	Window window;
	struct Client *next;
	struct Client *prev;
	int saved_x, saved_y;
	unsigned int saved_w, saved_h;
	int has_saved_state;
} Client;

typedef struct {
	char *name;
	char *exec;
} LauncherItem;

typedef struct {
	Display *dpy;
	Window root;
	Window active;
	int screen;
	XEvent ev;
	XButtonEvent start;
	XWindowAttributes attr;

	Cursor cursor_default;
	Cursor cursor_move;
	Cursor cursor_resize;

	Colormap cmap;
	Borders borders;

	int running;
	int restart;

	unsigned int current_desktop;
	XftFont *font;
	XftFont *launcher_font;
	XftDraw *xft_draw;
	XftColor xft_color;
	XftColor xft_bg_color;
	XftColor xft_root_bg_color;
	XftColor xft_widget_color;
	XftColor xft_mic_active_bg;
	XftColor xft_mic_muted_bg;
	XftColor xft_mic_active_fg;
	XftColor xft_mic_muted_fg;
	XftColor xft_layout_tile_bg;
	XftColor xft_layout_float_bg;
	XftColor xft_layout_tile_fg;
	XftColor xft_layout_float_fg;

	XftColor xft_launcher_bg;
	XftColor xft_launcher_border;
	XftColor xft_launcher_fg;
	XftColor xft_launcher_hl_bg;
	XftColor xft_launcher_hl_fg;

	unsigned long last_widget_update;
	Client *clients;

	int is_cycling;
	Window cycle_win;
	Window *cycle_clients;
	int cycle_count;
	int active_cycle_index;

	// Layout management
	LayoutMode layout_modes[NUM_DESKTOPS + 1]; // 1-indexed for convenience

	// PulseAudio
	pa_threaded_mainloop *pa_mainloop;
	pa_context *pa_ctx;
	int mic_muted;

	// Launcher
	int launcher_active;
	Window launcher_win;
	LauncherItem *launcher_items;
	int launcher_items_count;
	LauncherItem **launcher_filtered;
	int launcher_filtered_count;
	char launcher_search[256];
	int launcher_selected;
} WindowManager;

typedef struct {
	int i;
	const char *s;
} Arg;

typedef struct {
	unsigned int mod;
	KeySym keysym;
	void (*func)(const Arg *);
	Arg arg;
} Keybinds;

typedef struct {
	unsigned int mod;
	KeySym keysym;
	const char *cmd;
} Shortcut;

void set_log_level(LogLevel level);
LogLevel get_log_level_from_env(void);
void log_message(FILE *stream, LogLevel level, const char* format, ...);

void init_window_manager(void);
void deinit_window_manager(void);
void handle_map_request(void);
void handle_unmap_notify(void);
void handle_destroy_notify(void);
void handle_property_notify(void);
void handle_motion_notify(void);
void handle_client_message(void);
void handle_button_press(void);
void handle_button_release(void);
void handle_key_press(void);
void handle_key_release(void);
void handle_focus_in(void);
void handle_focus_out(void);
void handle_enter_notify(void);
void handle_expose(void);
void handle_configure_request(void);

Window get_active_window(void);
void set_active_window(Window window, Time time);
void set_active_border(Window window);
void grab_buttons(Window window);
void get_cursor_offset(Window window, int *dx, int *dy);
int window_exists(Window window);
int ignore_x_error(Display *dpy, XErrorEvent *err);

void move_window_x(const Arg *arg);
void move_window_y(const Arg *arg);
void resize_window_x(const Arg *arg);
void resize_window_y(const Arg *arg);
void window_snap_up(const Arg *arg);
void window_snap_down(const Arg *arg);
void window_snap_left(const Arg *arg);
void window_snap_right(const Arg *arg);
void window_hmaximize(const Arg *arg);
void window_vmaximize(const Arg *arg);
void close_window(const Arg *arg);

void quit(const Arg *arg);
void reload(const Arg *arg);

void goto_desktop(const Arg *arg);
void send_window_to_desktop(const Arg *arg);
void toggle_pip(const Arg *arg);
int is_sticky(Window window);
int is_always_on_top(Window window);
void update_client_list(void);
void toggle_always_on_top(const Arg *arg);
void execute_shortcut(const char *command);
void cycle_active_window(const Arg *arg);
void end_cycling(void);
void toggle_fullscreen(const Arg *arg);
void center_window(const Arg *arg);

void widget_desktop_indicator(void);
void widget_datetime(void);
void widget_mic_indicator(void);
void widget_layout_indicator(void);
void redraw_widgets(void);

void init_audio(void);
void deinit_audio(void);
void toggle_mic_mute(const Arg *arg);

void apply_tiling_layout(void);
void toggle_layout(const Arg *arg);

void toggle_launcher(const Arg *arg);
void launcher_handle_key(void);
void launcher_draw(void);

#endif // GLITCH_H
