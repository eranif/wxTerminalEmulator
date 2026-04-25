# Codebase Information

## Basic Information

| Property | Value |
|----------|-------|
| **Project Name** | wxTerminalEmulator |
| **Repository** | git@github.com:eranif/wxTerminalEmulator.git |
| **Primary Language** | C++20 |
| **Build System** | CMake (minimum 3.10) |
| **GUI Framework** | wxWidgets 3.2.x |
| **License** | (See LICENSE file) |

## File Structure

```
wxTerminalEmulator/
├── CMakeLists.txt          # Main build configuration
├── README.md               # User-facing documentation
├── SUMMARY.md              # Development history and fixes log
├── LICENSE                 # License file
├── TODO.md                 # Pending tasks
├── .clang-format           # Code formatting rules
├── .gitignore              # Git ignore patterns
├── wx.rc                   # Windows resource file
├── main.cpp                # Demo application entry point
├── app_persistence.h/cpp   # Settings persistence
├── terminal_core.h/cpp     # Terminal emulation engine
├── terminal_view.h/cpp     # wxWidgets terminal panel
├── terminal_event.h/cpp    # Custom wxWidgets events
├── terminal_theme.h        # Color scheme definitions
├── terminal_logger.h/cpp   # Debug logging system
├── pty_backend.h           # Abstract PTY backend interface
├── pty_backend_windows.h/cpp  # Windows ConPTY implementation
├── pty_backend_posix.h/cpp    # POSIX forkpty implementation
├── .build-debug/           # Debug build directory
├── .build-release/         # Release build directory
├── .github/workflows/      # CI/CD configurations
│   ├── macos.yml           # macOS CI build
│   ├── msys2.yml           # Windows MSYS2 CI build
│   └── ubuntu.yml          # Ubuntu CI build
└── .agents/summary/        # Generated documentation
```

## Build Targets

| Target | Type | Description |
|--------|------|-------------|
| `wxterminal_lib` | Static Library | Core terminal emulation library |
| `wxterminal` | Executable | Demo application (optional, `BUILD_WXTERMINAL_DEMO`) |

## Supported Platforms

| Platform | Backend | Compiler |
|----------|---------|----------|
| Windows (MinGW) | ConPTY | Clang |
| macOS | POSIX PTY | Clang |
| Linux | POSIX PTY | GCC/Clang |

## Key Technologies

- **wxWidgets**: Cross-platform GUI framework
- **CMake**: Build system generator
- **ConPTY**: Windows pseudo-console API (Windows 10 Build 17763+)
- **forkpty**: POSIX pseudo-terminal API
- **ANSI/VT100**: Terminal escape sequence protocol
- **UTF-8**: Unicode text encoding
