# AGENTS.md — wxTerminalEmulator

A concise guide for AI agents working with the wxTerminalEmulator codebase.

## Project Overview

wxTerminalEmulator is a cross-platform terminal emulation library for wxWidgets applications, written in C++20. It provides a `wxTerminalViewCtrl` panel that embeds a fully functional terminal into wxWidgets applications.

**Key facts:**
- Static library (`wxterminal_lib`) + demo executable (`wxterminal`)
- C++20, CMake 3.10+, wxWidgets 3.2.x
- Windows (ConPTY), macOS (forkpty), Linux (forkpty)
- UTF-8, ANSI/VT100 escape sequences, 256-color + true color support

## Documentation Ecosystem

This repo contains two levels of AI-oriented documentation:

| Resource | Location | Purpose |
|----------|----------|---------|
| **This file (AGENTS.md)** | Repository root | Quick-start guide for common tasks |
| **Detailed docs** | `.agents/summary/*.md` | Deep-dive reference for architecture, APIs, data models, and workflows |

### How to Use

1. **Start here (AGENTS.md)** for most tasks — it covers directory layout, key entry points, build instructions, gotchas, and patterns.
2. **Go deeper into `.agents/summary/`** only when you need detailed understanding:
   - `index.md` — map of all documentation files; use this as the entry point for deep dives
   - `architecture.md` — system architecture, design patterns, component interactions
   - `components.md` — detailed component responsibilities and APIs
   - `interfaces.md` — public API reference, event system, integration patterns
   - `data_models.md` — data structures (Cell, Lines, ColourSpec, etc.)
   - `workflows.md` — data flow, rendering pipeline, escape sequence parsing, resize handling
   - `dependencies.md` — external dependencies and build requirements
   - `review_notes.md` — known documentation gaps and recommendations

> **Rule of thumb:** If AGENTS.md doesn't answer your question, read `.agents/summary/index.md` and follow the pointers to the relevant detailed file.

## Directory Organization

```
wxTerminalEmulator/
├── terminal_core.h/cpp      # Terminal emulation engine (framework-agnostic)
├── terminal_view.h/cpp      # wxWidgets panel (rendering + input)
├── pty_backend.h            # Abstract PTY interface
├── pty_backend_windows.cpp  # Windows ConPTY implementation
├── pty_backend_posix.cpp    # Linux/macOS forkpty implementation
├── terminal_event.h/cpp     # Custom wxWidgets events
├── terminal_theme.h         # Color schemes (dark/light presets)
├── terminal_logger.h/cpp    # Debug logging system
├── app_persistence.h/cpp    # Demo app settings persistence
├── main.cpp                 # Demo application (multi-tab notebook)
├── CMakeLists.txt           # Build configuration
└── .github/workflows/       # CI: macos.yml, msys2.yml, ubuntu.yml
```

## Key Entry Points

| Component | File | Purpose |
|-----------|------|---------|
| **TerminalCore** | `terminal_core.h` | Core API for terminal emulation logic |
| **wxTerminalViewCtrl** | `terminal_view.h` | wxWidgets embeddable terminal panel |
| **PtyBackend** | `pty_backend.h` | Interface for platform PTY implementations |
| **Demo App** | `main.cpp` | Example usage with notebook, themes, menus |

## Repo-Specific Patterns

### Escape Sequence Parsing
- Parser state is in `TerminalCore::m_inEscape` and `m_escape` buffer
- CSI sequences: `ESC[...` → `ParseEscape()` → parameter parsing → command execution
- OSC sequences: `ESC]...BEL` or `ESC]...ESC\` → title changes, etc.
- **Critical bug history**: `[` and `]` were once treated as escape terminators, breaking CSI/OSC parsing. Fixed by excluding them from single-char terminator check.

### Rendering Strategies
The view has three rendering paths selected at runtime:
- `RenderRowWithGrouping()` — groups adjacent cells with identical attributes (fastest)
- `RenderRowNoGrouping()` — cell-by-cell rendering (most compatible)

### Platform Backend Selection
Backends are selected at **compile time** via CMake:
```cmake
if(WIN32)
    list(APPEND LIB_SOURCES pty_backend_windows.cpp)
else()
    list(APPEND LIB_SOURCES pty_backend_posix.cpp)
endif()
```

Factory: `PtyBackend::Create(wxEvtHandler*)` returns `std::unique_ptr<PtyBackend>`.

### Threading Model
PTY backends spawn an I/O thread for reading child process output. The `on_output` callback is invoked from this thread and typically calls `wxTerminalViewCtrl::Feed()`, which schedules a UI update. The GUI thread handles rendering via `wxTimer` or paint events.

### Keyboard Input Handling
- Uses `wxEVT_CHAR_HOOK` to intercept Enter/Tab/Escape before default navigation
- `GetUnicodeKey()` doesn't work properly on Windows for `KEY_DOWN`, so Shift state is handled manually with a `shiftMap`
- `AcceptsFocus()` and `AcceptsFocusFromKeyboard()` are overridden to `true`

### Selection System
Two selection mechanisms coexist:
- **Mouse selection**: `LinearSelection` with anchor/current points in viewport coordinates
- **API selection**: `ApiSelection` for programmatic selection
- Both are rendered with theme's `selectionBg`/`selectionFg` colors

## Build Configuration

### CMake Options
| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_WXTERMINAL_DEMO` | `ON` | Build demo executable |
| `WXWIN` | (required on MinGW) | wxWidgets install path |
| `WXCFG` | `clang_x64_dll/mswu` | wxWidgets config (MinGW) |

### Windows (MinGW) Build
```bash
cd .build-debug
cmake .. -DWXWIN=C:/msys64/home/eran/root -DCMAKE_BUILD_TYPE=Debug
make -j32
```

### Linux/macOS Build
```bash
cd .build-debug
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j32
```

## Custom Events

| Event | Fired When | Handler Signature |
|-------|-----------|-------------------|
| `wxEVT_TERMINAL_TITLE_CHANGED` | OSC 0/2 title sequence | `evt.GetTitle()` |
| `wxEVT_TERMINAL_TERMINATED` | Child process exits | No payload |
| `wxEVT_TERMINAL_TEXT_LINK` | Ctrl+click on text | `evt.GetClickedText()` |
| `wxEVT_TERMINAL_BELL` | BEL character received | No payload |

## Common Gotchas

1. **GUI testing**: This is a GUI application. AI agents cannot interact with it directly. Always ask the user to test.

2. **Windows ConPTY version**: Requires Windows 10 Build 17763+. The code checks API availability at runtime.

3. **Resize behavior**: `TerminalCore::Resize()` preserves content. It previously called `Reset()` which cleared everything — this was fixed.

4. **Backspace**: Sends `0x7F` (DEL), not `0x08` (BS), to match standard terminal behavior expected by cmd.exe.

5. **Line feed**: `\n` (LF) only moves cursor down; `\r` (CR) moves to column 0. `\r\n` together gives "next line" behavior.

6. **Font caching**: `UpdateFontCache()` creates cached font variants (bold, underlined). Must be called after theme/font changes.

7. **Safe drawing mode**: `EnableSafeDrawing(true)` enables a compatibility rendering mode. Exact behavior is platform-dependent.

## Integration Example

```cpp
#include "terminal_view.h"

// Create terminal with default shell
wxTerminalViewCtrl* term = new wxTerminalViewCtrl(parent, "", std::nullopt);
term->SetTheme(wxTerminalTheme::MakeDarkTheme());

// Handle title changes
term->Bind(wxEVT_TERMINAL_TITLE_CHANGED, [](wxTerminalEvent& evt) {
    frame->SetTitle(evt.GetTitle());
});

// Send input
term->SendCommand("ls -la");  // Sends text + Enter
```

## Custom Instructions

<!-- This section is maintained by developers and agents during day-to-day work.
     It is NOT auto-generated by codebase-summary and MUST be preserved during refreshes.
     Add project-specific conventions, gotchas, and workflow requirements here. -->
