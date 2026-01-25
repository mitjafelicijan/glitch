Glitch is a minimal X11 window manager controlled by keyboard shortcuts.

> [!IMPORTANT]
> I built this window manager for personal use. There are no guarantees of
> stability. Main purpose is to learn X11 and also to have a window manager that
> is simple and that I can easily modify to my liking.

<img width="2276" height="1380" alt="glitch" src="https://github.com/user-attachments/assets/03692400-303b-40fe-99e8-b134cc09d47e" />

## Key Features

- **Window Movement**: Move windows by pixel values
- **Window Resizing**: Resize windows
- **Desktop Management**: Multiple desktops and moving windows between them
- **Window Control**: Kill, fullscreen, PiP, and always-on-top
- **Maximization**: Vertical and horizontal maximization
- **Edge Snapping**: Snap windows to screen edges
- **Window Centering**: Center windows on screen
- **Live Reload**: Reload configuration without restart

## Technical Details

- Built on X11/Xlib for low-level window management
- Uses EWMH (Extended Window Manager Hints) for fullscreen and state functionality
- Maintains state for maximized windows to enable toggle behavior
- Implements proper X11 event handling and window attribute management

## Requirements

- C compiler (GCC or Clang)
- GNU Make
- pkg-config
- X11 and Freetype development libraries

### Installing Dependencies

**Void Linux:**

```sh
sudo xbps-install libX11-devel freetype-devel pkg-config
```

## Compilation

```sh
# Build normally
make

# Use a specific compiler
CC=clang make
CC=gcc make

# Build with debug symbols
DEBUG=1 make

# Compile with optimization levels
OPTIMIZE=2 make

# Clean build
make clean && make
```

### Testing in Virtual Display

For safe testing without affecting your main session:

```sh
# Start Xephyr virtual display (requires Xephyr installed)
make virt

# In another terminal, run the window manager in the virtual display
DISPLAY=:69 ./glitch
```

## Installation

```sh
# Install to /usr/local/bin by default
sudo make install
```

## Running Glitch

### Starting the Window Manager

**From a display manager (login screen):**
- Add Glitch to your display manager's session list
- Select it from the session menu

```sh
# Exit current window manager first, then:
./glitch

# Start X server and window manager
startx ./glitch
```

## Configuration

Glitch uses a simple configuration system based on C header files. The
configuration is compiled into the binary, so you need to recompile after making
changes.

### Setting Up Configuration

1. **Copy the default configuration**:
   ```sh
   cp config.def.h config.h
   ```

2. **Edit your configuration**:
   ```sh
   vim config.h  # or your preferred editor
   ```

3. **Recompile the window manager**:
   ```sh
   make clean && make
   ```

4. **Restart or Reload**:
   - Quit and restart, or
   - Use `Mod+Shift+r` to reload in-place

   You can also sen `SIGUSR1` to trigger restart with
   ```sh
   kill -s SIGUSR1 $(pidof glitch)
   ```

### Configuration Structure

The configuration uses two main arrays:

1. **`shortcuts[]`** - Maps keys to shell commands
2. **`keybinds[]`** - Maps keys to window manager functions

### Default Key Bindings

Modifier key: `Mod4` (Super/Windows key)

#### Window Movement

```c
{ MODKEY,               XK_Left,    move_window_x,       { .i = -75 } },
{ MODKEY,               XK_Right,   move_window_x,       { .i = +75 } },
{ MODKEY,               XK_Up,      move_window_y,       { .i = -75 } },
{ MODKEY,               XK_Down,    move_window_y,       { .i = +75 } },
{ MODKEY,               XK_c,       center_window,       { 0 } },
```

#### Window Resizing

```c
{ MODKEY | ShiftMask,   XK_Left,    resize_window_x,     { .i = -75 } },
{ MODKEY | ShiftMask,   XK_Right,   resize_window_x,     { .i = +75 } },
{ MODKEY | ShiftMask,   XK_Up,      resize_window_y,     { .i = -75 } },
{ MODKEY | ShiftMask,   XK_Down,    resize_window_y,     { .i = +75 } },
```

#### Desktop Management

```c
// Switch to desktop
{ MODKEY,               XK_1,       goto_desktop,        { .i = 1 } },
// ... up to XK_9

// Move window to desktop
{ MODKEY | ShiftMask,   XK_1,       send_window_to_desktop, { .i = 1 } },
// ... up to XK_9
```

#### Window Control

```c
{ MODKEY,               XK_f,       toggle_fullscreen,   { 0 } },
{ MODKEY,               XK_q,       close_window,        { 0 } },
{ MODKEY | ShiftMask,   XK_q,       quit,                { 0 } },
{ MODKEY | ShiftMask,   XK_r,       reload,              { 0 } },
{ MODKEY | ShiftMask,   XK_s,       toggle_pip,          { 0 } },
{ MODKEY | ShiftMask,   XK_t,       toggle_always_on_top,{ 0 } },
{ Mod1Mask,             XK_Tab,     cycle_active_window, { .i = 0 } },
```

#### Window Maximization

```c
{ MODKEY,               XK_x,       window_hmaximize,    { 0 } },
{ MODKEY,               XK_z,       window_vmaximize,    { 0 } },
```

#### Window Snapping

```c
{ MODKEY | ControlMask, XK_Up,      window_snap_up,      { 0 } },
{ MODKEY | ControlMask, XK_Down,    window_snap_down,    { 0 } },
{ MODKEY | ControlMask, XK_Right,   window_snap_right,   { 0 } },
{ MODKEY | ControlMask, XK_Left,    window_snap_left,    { 0 } },
```

### Shell Commands

Defined in `shortcuts[]` array:
- `Mod+Return`: Terminal (st)
- `Mod+p`: Application launcher (rofi)
- `Mod+w`: Browser (brave)
- `Mod+e`: File Manager (thunar)
- `Mod+s`: Screen magnifier (xmagnify)
- `Control+Escape`: Screenshot (maim)

## Function Reference

| Function | Category | Parameters | Description |
| --- | --- | --- | --- |
| `move_window_x` | Movement | `arg->i` (pixels) | Move window horizontally (positive = right) |
| `move_window_y` | Movement | `arg->i` (pixels) | Move window vertically (positive = down) |
| `resize_window_x` | Resize | `arg->i` (pixels) | Resize window width (positive = wider) |
| `resize_window_y` | Resize | `arg->i` (pixels) | Resize window height (positive = taller) |
| `center_window` | Movement | None | Center window on screen |
| `window_snap_up` | Snap | None | Snap window to top edge |
| `window_snap_down` | Snap | None | Snap window to bottom edge |
| `window_snap_left` | Snap | None | Snap window to left edge |
| `window_snap_right` | Snap | None | Snap window to right edge |
| `goto_desktop` | Desktop | `arg->i` (desktop #) | Switch to specified desktop |
| `send_window_to_desktop` | Desktop | `arg->i` (desktop #) | Move window to specified desktop |
| `cycle_active_window` | Focus | `arg->i` (0=fwd, 1=back) | Cycle focus through windows |
| `close_window` | Control | None | Gracefully close active window |
| `quit` | Control | None | Exit the window manager |
| `toggle_fullscreen` | Control | None | Toggle fullscreen mode |
| `toggle_pip` | Control | None | Toggle Picture-in-Picture mode |
| `toggle_always_on_top` | Control | None | Toggle Always-on-Top status |
| `window_hmaximize` | Maximize | None | Toggle horizontal maximize |
| `window_vmaximize` | Maximize | None | Toggle vertical maximize |
| `reload` | System | None | Reload configuration/restart WM |
