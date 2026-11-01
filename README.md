# wxTerminalEmulator

[![GlyphT demo](wxterminal.gif)](wxterminal.gif)

[Open a larger preview](wxterminal.gif)

A wxWidgets-based terminal emulator project with two deliverables:

- A static library: `wxterminal_lib`
- A demo application: **GlyphT** (`glypht`)

## Overview

wxTerminalEmulator provides a cross-platform terminal emulation solution for wxWidgets applications. The current codebase
includes a terminal core, a wxWidgets terminal view, platform PTY backends, custom terminal events, theming support,
logging, and a demo program that exercises the API.

**GlyphT** is the demo terminal application that showcases the library's capabilities.

All you need is **wxWidgets** and **CMake** — no third-party libraries are required.

## Features

### Prerequisites

- CMake 3.10 or later
- C++20 compatible compiler
- wxWidgets 3.2.x or 3.3.x
- Platform-specific requirements:
  - **Windows**: MinGW with Windows 10+ (for `ConPTY` support)
  - **macOS**: macOS 10.10+ with CoreText and OpenGL support
  - **Linux**: libutil for PTY support and the GTK 3 and OpenGL development packages (`build.sh` builds wxWidgets with OpenGL). The OpenGL renderer is off by default; to turn it on (`USE_OPENGL=1`) you also need GLEW (see [OpenGL on Linux](#opengl-on-linux))
  - **FreeBSD**: the same as Linux (GTK 3, OpenGL, and GLEW for `USE_OPENGL=1`), plus bash, GNU make and pkg-config: `pkg install bash cmake git gmake pkgconf gtk3 glew` (see [Building on FreeBSD](#building-on-freebsd))

### Rendering

#### **🚀 OpenGL Acceleration (all platforms)**
- **High-performance GPU-accelerated rendering** using a glyph-atlas renderer on Windows, macOS and Linux (on by default on Windows and macOS, off by default on Linux)
- **wxGLCanvas-based implementation** with batched draw calls
- **Lazy glyph rasterization** with per-frame caching for optimal performance
- **DPI-aware scaling** for crisp text on high-resolution displays
- **Configurable rendering backend**: Enable/disable via `-DUSE_OPENGL=ON/OFF` at build time
- **Legacy wxDC fallback** available for compatibility (the default on Linux)

#### **Display & Theming**
- **Dark and light themes** via `terminal_theme.h`
- **Native appearance support** with CLI option `--appearance=<light|dark|auto>` (platform-native look). It needs wxWidgets 3.3 or newer: with wxWidgets 3.2, which `build.sh` uses by default on Windows, the option is accepted but has no effect, and the app uses the look of the system.
- **Custom font selection** in the demo application
- **Mouse selection, copy/paste, and programmatic selection**
- **Clickable text/links** with Ctrl+click to detect tokens and emit link events
- **Double-click word selection** with configurable delimiter characters
- **Resize overlay visualization** showing terminal dimensions while resizing

### Terminal Emulation
- **VT100/ANSI escape sequence handling** in `terminal_core.cpp`
- **UTF-8 / Unicode character support**
- **Scrollback buffer** and viewport management
- **Alternate screen buffer** support
- **Scroll regions** and cursor save/restore handling
- **OSC/CSI processing**, including title updates and terminal responses
- **Progress bar (OSC 9;4)**, as in iTerm2, ConEmu and Windows Terminal: the program reports a state and a percent, the application draws the bar
- **Program status (OSC 7501)**, the [Program Status Protocol](https://www.swareco.com/blog/osc-7501-program-status-protocol) (revision 0.4) used by Kiro CLI: `idle`, `working`, `done`, `blocked`, `error` and `clear`, with an optional percent, id, app, title and message. The terminal also answers the support query `ESC ] 7501 ; ? ESC \`

### Platform Support
- **Windows**: ConPTY-based backend with OpenGL rendering support
- **macOS**: PTY backend with OpenGL acceleration and native appearance integration
- **Linux**: PTY backend with legacy wxDC rendering by default; OpenGL rendering is supported and can be enabled with `-DUSE_OPENGL=ON`
- The build automatically selects the appropriate PTY implementation and rendering backend

### Input Handling
- **Printable character input**
- **Special key translation** for navigation, insert/delete, page keys, and function keys
- **Common terminal shortcuts** such as Ctrl+C, Ctrl+L, Ctrl+U, Ctrl+K, Ctrl+W, Ctrl+Z, Ctrl+R, Ctrl+D, Ctrl+A, Ctrl+E
- **Clipboard integration** through the terminal view
- **Working directory support** for launched processes

## Architecture

### Core Components

#### `TerminalCore` (`terminal_core.h/cpp`)
The heart of the terminal emulation engine:
- Manages the terminal buffer (deque of cell rows)
- Parses and processes ANSI/VT100 escape sequences
- Handles cursor positioning and text attributes
- Implements scrollback and viewport management
- Supports alternate screen buffer
- Provides callback mechanisms for title changes and terminal responses

#### `wxTerminalViewCtrl` (`terminal_view.h/cpp`)
wxWidgets panel that provides the visual interface:
- Renders terminal content using wxDC/wxGCDC
- Handles user input (keyboard and mouse)
- Manages text selection and clipboard operations
- Implements scrolling with mouse wheel support
- Provides font caching for performance
- Auto-updates display with timer-based refresh
- Supports safe drawing mode, buffer navigation, and line centering
- `AddHeader(text, fg, bg, font)` / `RemoveHeader()`: a strip (one text row high; colours and font are optional) with a separator line at the top of the view. It is not part of the terminal: it never scrolls, the program gets that many fewer rows, and the mouse does not select or report over it. Works in both the OpenGL and the wxDC renderers
- `AddFooter(text, fg, bg, font)` / `RemoveFooter()`: the same, at the bottom of the view (the separator line is above the text). A view can have both
- `ShowProgressBar(bool)` / `IsShowProgressBar()`: show a thin native progress bar (a `wxGauge`) above the view. It shows the progress of the program (OSC 9;4, or OSC 7501 for a working or blocked program): a bar with a percent, or a moving bar when there is no percent. The gauge is a child of the parent window and is inserted at the start of the parent's sizer, so call this after the parent has its (vertical) sizer. If the parent has no sizer, or is a `wxBookCtrlBase` (a notebook, whose pages share the parent), nothing happens and `IsShowProgressBar()` stays false. The gauge is created once; after that it is only shown or hidden: it is shown while there is progress (and the view is shown), and a hidden gauge takes no room, so the terminal is a few pixels smaller only while there is progress. Off by default
- `SetProgressColour(colour)` / `GetProgressColour()`: the colour of the progress bar. The default (also `wxNullColour`) is the green of the theme, and it follows the theme. The getter returns the colour in use, never an invalid one
- `SetProgressEdgeColour(colour)` / `GetProgressEdgeColour()`: the colour of the edges of the progress bar (the tail and head of the moving bar, and the leading end of the filled bar). It blends into the progress colour towards the middle. The default (also `wxNullColour`) is the yellow of the theme, and it follows the theme. Pass the progress colour for a bar with one colour
- `SetProgressBackgroundColour(colour)` / `GetProgressBackgroundColour()`: the background of the progress bar, behind the bar. The default (also `wxNullColour`) is the background colour of the theme, and it follows the theme. The colours are used as they are, nothing is adjusted for contrast. Only the OpenGL gauge has colours of its own; the native `wxGauge` keeps the colours of the system

#### PTY Backend (`pty_backend.h`)
Abstract interface for platform-specific pseudo-terminal implementations:
- **Windows**: `WindowsPtyBackend` - Uses Windows ConPTY API for modern pseudo-console support
- **POSIX**: `PosixPtyBackend` - Uses `forkpty` for Linux, FreeBSD and macOS

Each backend provides:
- Process launching with specified command (or default shell)
- Bidirectional I/O with the child process
- Terminal resizing support
- Threaded I/O to prevent blocking

The demo supports launching a default shell or a custom command, and it can also pass environment variables to the spawned process.

#### Terminal Events (`terminal_event.h/cpp`)
Custom wxWidgets events for terminal-specific notifications:
- `wxEVT_TERMINAL_TITLE_CHANGED`: Fired when terminal changes its title via OSC sequences
- `wxEVT_TERMINAL_TERMINATED`: Fired when the shell/process exits
- `wxEVT_TERMINAL_TEXT_LINK`: Fired when user Ctrl+clicks on text to select a token/link
  - The event carries the clicked text string for custom handling (e.g., opening URLs, file paths)
- `wxEVT_TERMINAL_APP_OWNS_MOUSE` / `wxEVT_TERMINAL_APP_RELEASE_MOUSE`: Fired when the program in the terminal (tmux, vim, ...) starts or stops tracking the mouse. While it tracks the mouse the cursor is an arrow, and Shift gives the mouse back to the terminal
- `wxEVT_TERMINAL_PROGRAM_STATUS`: Fired for each valid OSC 7501 report (`ESC ] 7501 ; state=working:progress=40 BEL`)
  - `evt.GetProgramStatus()` returns a `wxTerminalProgramStatus` with `state` (`wxTerminalProgramState`: `Idle`, `Working`, `Done`, `Blocked`, `Error`, `Clear`), `kind` (`wxTerminalBlockedKind`), `progress` (0-100, or -1 when unknown), `id`, `app`, `title` and `message`. The same values are available through `evt.GetProgramState()`, `GetBlockedKind()`, `GetProgramProgress()`, `GetProgramId()`, `GetProgramApp()`, `GetProgramTitle()` and `GetProgramMessage()`. The library keeps no records: every report is an event. `wxTerminalViewCtrl::GetProgramStatus()` returns the latest report without an id (the root record); it is cleared when a working or blocked program ends
- `wxEVT_TERMINAL_PROGRESS`: Fired when the program sets or clears a progress bar with OSC 9;4 (`ESC ] 9 ; 4 ; <state> ; <value> BEL`)
  - `evt.GetProgressState()` returns a `wxTerminalProgressState` (`Hidden`, `Normal`, `Error`, `Indeterminate`, `Warning`) and `evt.GetProgressValue()` returns 0-100. `wxTerminalViewCtrl::GetProgressState()` / `GetProgressValue()` return the current values. `evt.ShowProgress()` / `evt.HideProgress()` are shortcuts for "state is not / is `Hidden`". The library does not draw the bar; the application does
- `wxEVT_TERMINAL_APP_SELECTION_CHANGED`: Fired when the program copies text with OSC 52 (write only, `?` queries are ignored), or when that text is cleared (left press, mouse tracking stops, `ClearAll()`)
  - `evt.GetString()` holds the text (empty after a clear). `GetAppSelection()` returns the current text and `ClearAppSelection()` forgets it. The system clipboard is not written; the application decides what to do with the text. Copies over about 3 KB arrive cut (libtsm limits an OSC to 4 KB) and are ignored if they are no longer valid UTF-8

#### Terminal Theme (`terminal_theme.h`)
Color scheme management:
- Defines foreground/background colors
- Standard 16 ANSI colors (normal + bright variants)
- Selection and highlight colors with alpha transparency
- Cursor color
- Helper methods for 256-color palette and true color conversion
- Link/clicked text color for visual feedback
- Pre-defined dark and light themes

#### Tab Painter (`GlyphTabPainter.h`)
Draws tabs with the colours of a `wxTerminalTheme`. It uses only plain wxWidgets (wxWidgets 3.2 or newer), and it is used by `GlyphTabBar` and `GlyphBook`:
- `GlyphTabPainter::Style::Pills` (the default): the tab bar has the colour of the terminal, each tab is a small rounded rectangle, and the current one is filled
- `GlyphTabPainter::Style::Browser`: a darker bar, and a current tab with rounded corners on the outer side that looks joined with the terminal
- The close button is shown on the current tab and on the tab under the mouse
- The shapes are anti-aliased when a `wxGraphicsContext` is available

#### Tab Art For `wxAuiNotebook` (`GlyphTabArt.h`)
The same tabs for a `wxAuiNotebook` (wxWidgets 3.3 or newer; it needs `wxAuiTabArtBase`). It is a thin adapter around `GlyphTabPainter`:
- Use it with `notebook->SetArtProvider(new GlyphTabArt(theme))`. After a theme change, give the notebook a new art
- The class is empty with an older wxWidgets, so check `wxCHECK_VERSION(3, 3, 0)` before you use it

#### Tabbed Book With Tabs On Any Side (`GlyphBook.h`)
`GlyphBook` is a `wxBookCtrlBase` with tabs at the top, at the bottom, on the left or on the right. It works with wxWidgets 3.2 and newer and does not use `wxAuiNotebook`: inside it is a `GlyphTabBar` (the tabs) and a `wxSimplebook` (the pages):
```cpp
auto *book = new GlyphBook(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                           wxAUI_NB_DEFAULT_STYLE | wxAUI_NB_LEFT);
book->SetTheme(theme);                      // colours of a wxTerminalTheme
book->AddPage(new wxTextCtrl(book->GetPagesParent(), wxID_ANY), "One", true);
book->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED, &MyFrame::OnPage, this);
book->SetTabPosition(wxRIGHT);              // or change the flags at any time
```
- The tab side is the window style: `wxAUI_NB_TOP`, `wxAUI_NB_BOTTOM`, `wxAUI_NB_LEFT` or `wxAUI_NB_RIGHT`. `SetTabPosition()` changes it. The close button flags and `wxAUI_NB_MIDDLE_CLICK_CLOSE` work too; the other `wxAUI_NB_*` flags are ignored
- The book sends the standard `wxEVT_BOOKCTRL_PAGE_CHANGING` (it can be vetoed) and `wxEVT_BOOKCTRL_PAGE_CHANGED`. The other events are `GlyphBookEvent` (a `wxBookCtrlEvent` with `GetGroup()` and `GetPosition()`, the position of the mouse in the book): `wxEVT_GLYPHBOOK_PAGE_CLOSE` (it can be vetoed) and `_PAGE_CLOSED` when the user closes a page; `_TAB_CONTEXT_MENU` and `_TAB_MIDDLE_UP` for a tab; `_GROUP_CONTEXT_MENU` and `_GROUP_MIDDLE_UP` for the header of a group; `_BAR_CONTEXT_MENU` and `_BAR_MIDDLE_UP` for the bar where there is no tab; `_GROUP_CHANGED` when the user collapses a group or drags a tab into or out of a group
- The book shows no menu: the application handles the context menu events, builds its own menu and calls the functions of the book
- Groups of tabs, like the tab groups of a browser: a group is a coloured chip with a name, followed by its tabs, with a line of the colour of the group along them. `AddPageToNewGroup()`, `AddPageToGroup()`, `RemovePageFromGroup()`, `Ungroup()`, `SetGroupName()`, `SetGroupColour()` and `SetGroupCollapsed()` change them. A click on the chip collapses or expands the group; a collapsed group shows only its chip. A group with the selected tab cannot be collapsed (`SetGroupCollapsed()` returns false), and a collapsed group opens when one of its tabs is selected. The tabs of a group are always next to each other; a tab dropped between two tabs of a group joins it, and a tab dropped anywhere else is out of any group. `GetGroupColours()` is a palette for a menu
- Create the pages with `GetPagesParent()` as their parent. Another parent works too, but the page is moved when it is added
- A click selects a page, the close button or a middle click closes it, and dragging a tab moves it. Tabs on the top or bottom get narrower when they do not fit, then the bar scrolls (mouse wheel) and a button shows the list of all tabs. Tabs on the left or right are rows: drag the edge next to the pages to change the width (`SetTabBarWidth()`)
- The pages cannot be split, and a tab cannot be dragged to another book
- `book->SaveLayout()` returns a `BookLayout` (`BookLayout.h`: the groups, and the label, the group and the selected mark of each tab), and `book->RestoreLayout(layout)` applies it. The application stores it
- `GlyphTabBar` (`GlyphTabBar.h`) is the bar. Use `GlyphBook` instead of using the bar directly
- In GlyphT, pick "Tab Position" (Top, Bottom, Left or Right) in the settings. It is saved as `tabposition=` in `config.ini`, and the width of the bar as `tabbarwidth=`

#### Terminal Logger (`terminal_logger.h/cpp`)
Debugging and diagnostics support:
- Configurable log levels (Trace, Debug, Warn, Error)
- Optional file output
- Stream-based logging API
- Function timing support with `LogFunction` helper
- Counter tracking for performance analysis

#### Terminal OpenGL Renderer (`terminal_gl_renderer.h/cpp`)
GPU-accelerated rendering path (all platforms; enabled by default on Windows and macOS, disabled by default on Linux):
- **Glyph atlas renderer** using a single-channel texture for efficient rasterization
- **Batched draw calls** (solid quads + textured glyphs) for minimal GPU overhead
- **Lazy glyph rasterization** on first sighting using wxWidgets font engine
- **DPI-aware scaling** for crisp rendering on high-resolution displays
- **Performance optimized** with efficient vertex buffer management
- **Metrics alignment** with legacy wxDC path for consistent appearance
- Compiles to no-op when `USE_OPENGL=OFF`

## Building

### Build Targets
- `wxterminal_lib` - static library
- `glypht` - demo application (enabled by default through `BUILD_GLYPHT`)

### Build Instructions

#### Using `build.sh`

The script builds everything on all platforms (macOS, Linux, FreeBSD and
Windows with MSYS2): it first downloads and builds wxWidgets (v3.2.12 on Windows and v3.3.4 on the other systems by default, with OpenGL
support) and then builds `glypht`. Everything goes into `.build-release/`. On
Linux, `configure` is run with `--with-opengl`, so the OpenGL development
packages must be installed (for example `libgl-dev`); the build stops with an
error if they are missing. FreeBSD is built like Linux, with `gmake` (GNU make)
in place of `make`; the package is a `glypht-<version>-freebsd-<arch>.tar.gz`
tarball.

```bash
./build.sh              # build wxWidgets (first run only) and glypht
./build.sh --cmake      # force the CMake configure step
./build.sh --opengl     # Linux and FreeBSD only: use the OpenGL renderer (the same as USE_OPENGL=1)
./build.sh clean        # make clean
./build.sh package      # build, then create the package for this OS
./build.sh distclean    # remove .build-release, including wxWidgets
```

#### OpenGL on Linux

To build with `USE_OPENGL=1` on Linux you need GLEW in addition to the GTK 3
development packages:

```bash
# Debian/Ubuntu
sudo apt install libgtk-3-dev libglew-dev
USE_OPENGL=1 ./build.sh

# Fedora
sudo dnf install gtk3-devel glew-devel
USE_OPENGL=1 ./build.sh
```

wxWidgets also needs the GL, EGL and wayland-egl development files (on
Debian/Ubuntu `libgl-dev`, `libegl-dev` and `libwayland-dev`). The GTK 3
development package already installs them. wxWidgets checks for EGL and
wayland-egl only when it is configured: without them it is built without EGL,
and the OpenGL view does not work in a Wayland session. If that happened,
install the missing packages and run `./build.sh distclean` before you build
again.

#### Building on FreeBSD

`build.sh` builds FreeBSD like Linux (wxWidgets with GTK 3), so it needs these
packages. Install them as root (with `su`, `sudo` or `doas`):

```sh
pkg install bash cmake git gmake pkgconf gtk3 glew
```

| Package   | Why                                                               |
|-----------|-------------------------------------------------------------------|
| `bash`    | `build.sh` is a bash script (FreeBSD installs it in `/usr/local/bin`) |
| `cmake`   | Configures GlyphT                                                 |
| `git`     | Downloads wxWidgets                                               |
| `gmake`   | GNU make: the Makefiles of wxWidgets do not work with BSD make    |
| `pkgconf` | Provides `pkg-config`, which the wxWidgets `configure` uses to find GTK 3 |
| `gtk3`    | The GTK 3 toolkit; FreeBSD packages include the headers, there is no `-dev` package |
| `glew`    | Only for the OpenGL renderer (`USE_OPENGL=1` or `--opengl`)       |

The compiler is `clang++`, which is part of the base system.

`gtk3` does not install `pkg-config`. Without `pkgconf`, `configure` stops with
"The pkg-config script could not be found or is too old". To check that
everything is in place before you build:

```sh
pkg info bash cmake git gmake pkgconf gtk3 glew   # "No package(s) matching" = not installed
pkg-config --modversion gtk+-3.0                  # prints the GTK 3 version
```

Then build as usual:

```sh
./build.sh              # or: ./build.sh --opengl
```

`build.sh` passes `/usr/local/include` and `/usr/local/lib` to the wxWidgets
`configure`, because the packages are installed there. To run GlyphT you also
need a graphical session (for example `pkg install xorg xfce`); it is not needed
to build.

Result:
- macOS: `.build-release/glypht.app`
- Linux and FreeBSD: `.build-release/glypht`
- Windows: `.build-release/install/bin/glypht.exe`

Environment variables:
- `WX_VERSION`: wxWidgets tag to build (default: `v3.2.12` on Windows, `v3.3.4` on the other systems)
- `WX_CONFIG`: path to an existing `wx-config`; skips the wxWidgets build (macOS, Linux, FreeBSD)
- `USE_OPENGL=0|1`: choose the renderer (default: ON on macOS and Windows, OFF on Linux and FreeBSD). On Linux and FreeBSD the option `--opengl` is the same as `USE_OPENGL=1`
- `MACOS_DEPLOYMENT_TARGET`: macOS deployment target (default: `13.0`)
- `GLYPHT_VERSION`: version used in the package name (default: `1.0.0`)

#### DEB package (Debian/Ubuntu)

On Debian based systems (Debian, Ubuntu, Mint, ...), `./build.sh package`
creates a `.deb` package next to the `.tar.gz`. It needs `dpkg-dev` (for
`dpkg-shlibdeps`, which finds the package dependencies). On other Linux
systems the `.deb` is skipped:

```bash
GLYPHT_VERSION=1.2.3 ./build.sh package   # .build-release/glypht_1.2.3_amd64.deb
sudo apt install ./.build-release/glypht_1.2.3_amd64.deb
```

In your own build tree, `make package` (or `cpack -G DEB`) does the same. The
package installs GlyphT into `/usr/lib/glypht`, the command `/usr/bin/glypht`,
a desktop entry and an icon.

## Usage

### Basic Integration

```cpp
#include "terminal_view.h"

// Create terminal view in your window
wxTerminalViewCtrl* terminal = new wxTerminalViewCtrl(parentWindow, "", std::nullopt);

// Set theme
terminal->SetTheme(wxTerminalTheme::MakeDarkTheme());

// Start a shell process
// The constructor starts the process; pass a command string there if needed.

// Or create the view with a specific command
wxTerminalViewCtrl* terminal2 = new wxTerminalViewCtrl(parentWindow, "/bin/bash", std::nullopt);
```

### Event Handling

```cpp
// Handle title changes
terminal->Bind(wxEVT_TERMINAL_TITLE_CHANGED, [](wxTerminalEvent& evt) {
    wxString title = evt.GetTitle();
    // Update window title, etc.
});

// Handle process termination
terminal->Bind(wxEVT_TERMINAL_TERMINATED, [](wxTerminalEvent& evt) {
    // Clean up or restart
});

// Handle text link clicks (Ctrl+click on text)
terminal->Bind(wxEVT_TERMINAL_TEXT_LINK, [](wxTerminalEvent& evt) {
    wxString clickedText = evt.GetText();
    // Handle the clicked text - open URL, file, etc.
    // Example: wxLaunchDefaultBrowser(clickedText);
});
```

### Sending Input

```cpp
// Send text to terminal
terminal->SendInput("ls -la\r"); // \r for Enter

// Send control sequences
terminal->SendInput("\x03"); // Ctrl+C
```

### Terminal Configuration

```cpp
// Set scrollback buffer size
terminal->SetBufferSize(10000); // lines

// Get current line count
size_t lines = terminal->GetLineCount();

// Center a specific line in view
terminal->CenterLine(lineNumber);

// Get text content of a line
wxString lineText = terminal->GetLine(lineNumber);

// Scroll to bottom
terminal->ScrollToLastLine();
```

### Text Selection

```cpp
// Programmatic selection
terminal->SetUserSelection(col, row, count);

// Clear selections
terminal->ClearUserSelection();
terminal->ClearMouseSelection();
```

### Clickable Text and Links

```cpp
// Ctrl+click on text automatically detects the token under cursor
// and fires wxEVT_TERMINAL_TEXT_LINK event with the clicked text

// Configure word delimiters for double-click selection
terminal->SetWordDelimiters(" \t\r\n()[]{}\"'<>|;"); // default delimiters

// The terminal view highlights clicked text with the theme's link color
// Visual feedback is provided during hover and click interactions

// Handle link clicks in your application
terminal->Bind(wxEVT_TERMINAL_TEXT_LINK, [](wxTerminalEvent& evt) {
    wxString text = evt.GetText();
    if (text.StartsWith("http://") || text.StartsWith("https://")) {
        wxLaunchDefaultBrowser(text);
    }
    // Add custom handling for file paths, error locations, etc.
});

// Handle progress (OSC 9;4)
terminal->Bind(wxEVT_TERMINAL_PROGRESS, [](wxTerminalEvent& evt) {
    if (evt.HideProgress()) {
        // Hide the bar
    } else { // evt.ShowProgress() is true
        int percent = evt.GetProgressValue(); // 0-100
        // Show the bar; Indeterminate means busy, no percent
    }
});
```

Test it from a shell: `printf '\e]9;4;1;50\a'` shows 50%, `printf '\e]9;4;0\a'` hides the bar.

## GlyphT — Demo Application

The included demo application (`main.cpp`) showcases the library features:

### Features
- Dark/Light theme switching
- The font [Iosevka](https://typeof.net/Iosevka/) (SIL Open Font License, see `assets/fonts/LICENSE.md`) is shipped with the app on all platforms and is the default font. It is loaded for GlyphT only and is not installed on the system
- Window sized to about half of the available screen area
- Menu options for:
  - New terminal tab
  - Theme selection
  - Font selection
  - Safe drawing toggle
  - Line centering by number
  - Programmatic text selection
  - Line content display
  - Direct input sending
  - Link click event handling with message box display
- Multiple terminal tabs in a notebook control
- Automatic terminal title updates
- A status bar message about the last progress event (OSC 9;4 or OSC 7501) of the active tab
- Tab switcher dialog (Ctrl+Tab, most recently used first; on macOS it is a menu shortcut in the Search menu)
- Process termination handling that closes tabs and exits when the last one closes
- Command-line log level control
- Optional shell override and environment list support

### Running GlyphT

```bash
./glypht [--log-level=<level>] [--appearance=<mode>] [--working-directory=<path>] [--shell=<command>]
```

**Log Levels**: `trace`, `debug`, `warn`, `error`

**Command-Line Options**:
- `--appearance=<mode>`: Control the light or dark look of the app (menus, tabs, dialogs): `light` or `dark` force it. `auto` (the default) follows the background colour of the terminal theme, and changes when you change the theme in Settings
- `--shell=<command>`: Launch a specific shell or command (default: the shell chosen in Settings, else the platform default shell). It wins over the Settings shell. The saved tabs are still restored, and they use this shell
- `--working-directory=<path>`: Start the shell in a specific directory (starts one fresh tab; the saved tabs are not restored)
- `--env=<list>`: Pass environment variables to the launched process
- `--log-level=<level>`: Set logging verbosity: `trace`, `debug`, `info`, `warn`, or `error` (default: `error`)

**Shell setting**: in Settings you can choose a shell program and its arguments. They are saved in `config.ini` (`shell=` and `shellargs=`) and used for new tabs and for restored tabs. `--shell` wins over them. When the program does not exist, or the setting is empty, the default shell of the platform is used. Tabs that are already open keep their shell.

## API Reference

### wxTerminalViewCtrl Public Methods

| Method | Description |
|--------|-------------|
| `bool StartProcess(const std::string& command)` | Start a shell/command in the terminal |
| `void SendInput(const std::string& text)` | Send text input to the terminal |
| `void Feed(const std::string& data)` | Directly feed data to terminal (for testing) |
| `void SetTheme(const wxTerminalTheme& theme)` | Change color scheme |
| `const wxTerminalTheme& GetTheme() const` | Get current theme |
| `void SetBufferSize(size_t maxLines)` | Set scrollback buffer size |
| `size_t GetBufferSize() const` | Get scrollback buffer size |
| `size_t GetLineCount() const` | Get total lines in buffer |
| `wxString GetLine(size_t line) const` | Get text content of a specific line |
| `void CenterLine(size_t line)` | Center specified line in viewport |
| `void ScrollToLastLine()` | Scroll to bottom of buffer |
| `void SetUserSelection(size_t col, size_t row, size_t count)` | Set programmatic selection |
| `void ClearUserSelection()` | Clear programmatic selection |
| `void ClearMouseSelection()` | Clear mouse selection |
| `std::string Contents() const` | Get flattened terminal content |
| `void SetWordDelimiters(const wxString& delims)` | Set delimiters for word/token detection |
| `wxString GetWordDelimiters() const` | Get current word delimiters |
| `void ClearClickedRange()` | Clear the currently clicked/highlighted text range |

### TerminalCore Public Methods

| Method | Description |
|--------|-------------|
| `void SetViewportSize(size_t rows, size_t cols)` | Resize terminal viewport |
| `void PutData(const std::string& data)` | Process raw terminal data |
| `void Reset()` | Reset terminal to initial state |
| `void ClearScreen()` | Clear visible screen area |
| `void MoveCursor(size_t row, size_t col)` | Position cursor |
| `void SetResponseCallback(...)` | Set callback for terminal responses |
| `void SetTitleCallback(...)` | Set callback for title changes |
| `size_t Rows() const` | Get viewport height |
| `size_t Cols() const` | Get viewport width |
| `wxPoint Cursor() const` | Get current cursor position |

#### Cell Selection and Text Extraction

The `TerminalCore` provides methods to extract text ranges and manage cell selections for implementing
clickable links and text selection features. See `terminal_core.h` for detailed API documentation.

## Terminal Capabilities

### Supported Escape Sequences

- **Cursor Movement**: CUU, CUD, CUF, CUB, CNL, CPL, CHA, CUP, HVP, VPA
- **Screen Manipulation**: ED, EL, SU, SD, ICH, DCH, IL, DL, ECH
- **Scrolling**: DECSTBM (set scroll region), RI (reverse index), IND (index)
- **Cursor Save/Restore**: DECSC, DECRC, SCP, RCP
- **Text Attributes**: SGR (all standard attributes plus 256-color and true color)
- **Mode Setting**: DECSET/DECRST (alternate screen, cursor visibility, etc.)
- **Device Reports**: DSR (cursor position), DA (device attributes)
- **OSC Sequences**: Title setting, color queries

### Color Support

- 16-color ANSI (8 normal + 8 bright)
- 256-color palette (16 + 216 RGB cube + 24 grayscale)
- 24-bit true color (16.7 million colors)

## License

MIT License - Copyright (c) 2026 Eran Ifrah

See [LICENSE](LICENSE) file for full details.

## Contributing

This is an open-source project. Contributions are welcome via pull requests.

## Platform-Specific Notes

### Windows
- Requires Windows 10+ for ConPTY support
- **OpenGL rendering enabled by default** with GLEW for GL extension management
- Uses glyph-atlas renderer (GPU-accelerated) for high performance
- Falls back to wxDC rendering if OpenGL unavailable
- Default font: Consolas
- Native appearance option integrates with Windows 11+ dark/light theme (wxWidgets 3.3 or newer; build with `WX_VERSION=v3.3.4 ./build.sh` to get it)

### macOS
- Uses BSD-style PTY functions from `util.h`
- **OpenGL rendering enabled by default** using native macOS OpenGL framework
- Uses glyph-atlas renderer for smooth, efficient rendering
- Optimized for Retina displays with DPI-aware scaling
- Default font: Menlo
- Native appearance option integrates with system dark/light theme preference

### Linux
- Requires `libutil` for PTY support
- **OpenGL rendering is disabled by default** (the build still uses wxDC). It is supported: enable it with `-DUSE_OPENGL=ON`
- Uses legacy wxDC rendering path (buffered, still performant)
- Link with `-lutil` flag
- Default font: System teletype font
- To enable OpenGL: use `-DUSE_OPENGL=ON`. It needs GLEW and wxWidgets built with the `gl` component. On WSL use `GDK_BACKEND=x11 DISPLAY=:0`, because WSLg Wayland EGL fails

### FreeBSD
- Built like Linux, with GTK 3; see [Building on FreeBSD](#building-on-freebsd) for the packages
- `forkpty` comes from `libutil.h` (linked with `-lutil`)
- **OpenGL rendering is disabled by default**, as on Linux. Enable it with `--opengl` (needs `glew`)
- Default font: the bundled `Iosevka`, size 16

## Performance

The library is optimized for high-performance terminal rendering:

### OpenGL Path (all platforms)
- **GPU-accelerated rendering** with glyph atlas caching
- **Minimal CPU overhead** via batched draw calls
- **Low-latency updates** with efficient vertex buffer management
- **Smooth 60 FPS rendering** even with large scrollback buffers
- **High-throughput output** capable of keeping up with compilation logs and streaming data

### Legacy wxDC Path (default on Linux, and when OpenGL is disabled)
- Batched text rendering reduces draw calls
- Font caching minimizes object creation
- Timer-based refresh prevents excessive redraws (only when needed)
- Efficient buffer management with deque structure
- Minimal allocations in hot paths

### Typical Performance
- Handles high-throughput output (compilation logs, etc.)
- Smooth scrolling even with large scrollback buffers
- Responsive input handling with sub-frame latency
- Resize overlay provides visual feedback during window resizing
