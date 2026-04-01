# wxTerminalEmulator

A wxWidgets-based terminal emulator project with two deliverables:

- A static library: `wxterminal_lib`
- A demo application: `wxterminal`

## Overview

wxTerminalEmulator provides a terminal emulation solution for wxWidgets applications. The current codebase includes a terminal core, a wxWidgets terminal view, platform PTY backends, custom terminal events, theming support, logging, and a demo program that exercises the API.

The project status is: the core library and demo app are present and functional in the repository.

## Features

### Terminal Emulation
- **VT100/ANSI escape sequence handling** in `terminal_core.cpp`
- **UTF-8 / Unicode character support**
- **Scrollback buffer** and viewport management
- **Alternate screen buffer** support
- **Scroll regions** and cursor save/restore handling
- **OSC/CSI processing**, including title updates and terminal responses

### Display Features
- **Dark and light themes** via `terminal_theme.h`
- **Custom font selection** in the demo application
- **Mouse selection, copy/paste, and programmatic selection**
- **Buffered rendering** with wxWidgets drawing APIs
- **Cross-platform view implementation** in `terminal_view.cpp`

### Platform Support
- **Windows**: ConPTY-based backend in `pty_backend_windows.cpp`
- **POSIX platforms**: PTY backend in `pty_backend_posix.cpp`
- The build links platform-specific system libraries as needed

### Input Handling
- **Printable character input**
- **Special key translation** for navigation, insert/delete, page keys, and function keys
- **Common terminal shortcuts** such as Ctrl+C, Ctrl+L, Ctrl+U, Ctrl+K, Ctrl+W, Ctrl+Z, Ctrl+R, Ctrl+D, Ctrl+A, Ctrl+E
- **Clipboard integration** through the terminal view

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

#### PTY Backend (`pty_backend.h`)
Abstract interface for platform-specific pseudo-terminal implementations:
- **Windows**: `WindowsPtyBackend` - Uses Windows ConPTY API for modern pseudo-console support
- **POSIX**: `PosixPtyBackend` - Uses `forkpty` for Linux and macOS

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

#### Terminal Theme (`terminal_theme.h`)
Color scheme management:
- Defines foreground/background colors
- Standard 16 ANSI colors (normal + bright variants)
- Selection and highlight colors with alpha transparency
- Cursor color
- Helper methods for 256-color palette and true color conversion
- Pre-defined dark and light themes

#### Terminal Logger (`terminal_logger.h/cpp`)
Debugging and diagnostics support:
- Configurable log levels (Trace, Debug, Warn, Error)
- Optional file output
- Stream-based logging API
- Function timing support with `LogFunction` helper
- Counter tracking for performance analysis

## Building

### Prerequisites
- CMake 3.10 or later
- C++20 compatible compiler
- wxWidgets 3.x
- Platform-specific requirements:
  - **Windows**: MinGW/MSVC with Windows 10+ (for ConPTY support)
  - **Linux**: libutil-dev (for forkpty)
  - **macOS**: Xcode command line tools

### Build Targets
- `wxterminal_lib` - static library
- `wxterminal` - demo application (enabled by default through `BUILD_WXTERMINAL_DEMO`)

### Build Instructions

#### Windows (MinGW)
```bash
mkdir build && cd build
cmake .. -DWXWIN=/path/to/wxWidgets -DWXCFG=clang_x64_dll/mswu
cmake --build .
```

#### Linux/macOS
```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### CMake Options
- `WXWIN`: Path to wxWidgets installation (required on Windows with MinGW)
- `WXCFG`: wxWidgets configuration string (Windows MinGW only, default: `clang_x64_dll/mswu`)
- `BUILD_WXTERMINAL_DEMO`: Enable/disable the demo executable

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

## Demo Application

The included demo application (`main.cpp`) showcases the library features:

### Features
- Dark/Light theme switching
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
- Multiple terminal tabs in a notebook control
- Automatic terminal title updates
- Process termination handling that closes tabs and exits when the last one closes
- Command-line log level control
- Optional shell override and environment list support

### Running the Demo

```bash
./wxterminal [--log-level=<level>]
```

Log levels: `trace`, `debug`, `warn`, `error`

Additional options:
- `--shell=<command>` to launch a specific shell or command
- `--env=<list>` to pass environment variables to the launched process

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
- Uses GCDC (Graphics Context DC) for proper Unicode rendering
- Default font: Consolas

### Linux
- Requires `libutil` for PTY support
- Default font: System teletype font
- Link with `-lutil` flag

### macOS
- Uses BSD-style PTY functions from `util.h`
- Default font: Menlo
- Optimized font size for Retina displays

## Performance

The library is optimized for performance:
- Batched text rendering reduces draw calls
- Font caching minimizes object creation
- Timer-based refresh prevents excessive redraws (only when needed)
- Efficient buffer management with deque structure
- Minimal allocations in hot paths

Typical performance:
- Handles high-throughput output (compilation logs, etc.)
- Smooth scrolling even with large scrollback buffers
- Responsive input handling with sub-frame latency
