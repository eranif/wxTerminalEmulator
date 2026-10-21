# AGENTS.md — wxTerminalEmulator

A short guide for AI agents working with the wxTerminalEmulator codebase.

`CLAUDE.md` in the repository root is a symbolic link to this file. Editing one
edits both.

## Agent General Guides

- Never add Co-Authored-By line to git commit.
- Answer concisely and briefly. Use B1-level English: simple vocabulary, short sentences, no rare words or idioms.

## Project Overview

wxTerminalEmulator is a cross-platform terminal emulation library for wxWidgets
applications, written in C++20. It provides a `wxTerminalViewCtrl` control that
embeds a working terminal into a wxWidgets application.

**Key facts:**

- Static library (`wxterminal_lib`) plus the demo application `glypht`
  ("GlyphT"), built from `src/glypht/`
- C++20, CMake 3.10+, wxWidgets 3.2.x (CI builds against `v3.2.8.1` on macOS
  and Linux, and against `v3.3.4` on Windows/MSYS2; 3.3.x also works)
- VT parsing is done by a vendored copy of **libtsm** in `libtsm/`
- PTY: Windows (ConPTY), macOS and Linux (`forkpty`)
- Two renderers: an OpenGL glyph atlas and the older wxDC renderer
- UTF-8, ANSI/VT100 escape sequences, 256 colors and true color

## Documentation Ecosystem

| Resource | Location | Purpose |
|----------|----------|---------|
| **This file (AGENTS.md)** | Repository root | Quick-start guide for common tasks |
| **Detailed docs** | `.agents/summary/*.md` | Deeper reference for architecture, APIs, data models, and workflows |

### How to Use

1. **Start here (AGENTS.md)** for most tasks. It covers the directory layout,
   key entry points, build instructions, gotchas, and patterns.
2. **Go deeper into `.agents/summary/`** only when you need more detail:
   - `index.md` — map of all documentation files; the entry point for deep dives
   - `codebase_info.md` — repository facts and statistics
   - `architecture.md` — system architecture, design patterns, component
     interactions
   - `components.md` — component responsibilities and APIs
   - `interfaces.md` — public API reference, event system, integration patterns
   - `data_models.md` — data structures (`Cell`, `Lines`, `ColourSpec`, etc.)
   - `workflows.md` — data flow, rendering pipeline, escape sequence parsing,
     resize handling
   - `dependencies.md` — external dependencies and build requirements
   - `review_notes.md` — known documentation gaps and recommendations

> **Rule of thumb:** if AGENTS.md does not answer your question, read
> `.agents/summary/index.md` and follow the pointers to the right file.

The `.agents/summary/` files are older than the code. Trust the code first.

## Directory Organization

```
wxTerminalEmulator/
├── src/lib/                       # The library
├── src/glypht/                    # GlyphT demo application
├── libtsm/                        # Vendored libtsm (VT parser), own CMake target `tsm`
├── cmake/                         # CMake helpers, e.g. FindWxWidgetsMSYS.cmake
├── assets/                        # Icons and images
├── build.sh                       # Builds wxWidgets and GlyphT (see Build Configuration)
├── CMakeLists.txt                 # Build configuration
└── .github/workflows/             # CI: macos.yml, msys2.yml, ubuntu.yml
```

## Key Entry Points

| Component | File | Purpose |
|-----------|------|---------|
| **TerminalCore** | `src/lib/terminal_core.h` | Terminal state and libtsm driver |
| **wxTerminalViewCtrl** | `src/lib/terminal_view.h` | Embeddable terminal control |
| **PtyBackend** | `src/lib/pty_backend.h` | Interface for platform PTY implementations |
| **TerminalGLRenderer** | `src/lib/terminal_gl_renderer.h` | OpenGL renderer, only when `USE_OPENGL=1` |
| **Demo app** | `src/glypht/main.cpp` | Example usage with tabs, themes, menus |

## Repo-Specific Patterns

### Escape Sequence Parsing

- All parsing is done by **libtsm**. `TerminalCore` owns a `tsm_screen` and a
  `tsm_vte` and feeds bytes into them.
- Callbacks go back to `TerminalCore`: `TsmWriteCb` (the terminal answers the
  program), `TsmOscCb` (title and other OSC), `TsmBellCb` (BEL),
  `TsmDrawCb` (one cell of the screen).
- The visible screen is copied into the cell snapshot after each update, so the
  renderer never reads libtsm state directly.
- Do not add a hand-written escape parser to `TerminalCore`. Older versions had
  one; it is gone.

### Rendering Strategies

Two independent choices:

1. **Renderer**, chosen at configure time with `USE_OPENGL`:
   - `USE_OPENGL=1`: the control is a `wxGLCanvas` and draws with
     `TerminalGLRenderer` (glyph atlas, two draw calls per frame).
   - `USE_OPENGL=0`: the control is a `wxPanel` and draws with wxDC.
2. **Row drawing inside the wxDC path**, chosen at runtime in `RenderRow()`:
   - `RenderRowWithGrouping()` — groups neighbor cells with equal attributes
     (faster, the default)
   - `RenderRowNoGrouping()` — cell by cell, used when
     `EnableSafeDrawing(true)` is set (slower, more accurate for wide or
     unusual glyphs)

### Platform Backend Selection

Backends are selected at **compile time** in `CMakeLists.txt`:

```cmake
if(WIN32)
    list(APPEND LIB_SOURCES src/lib/pty_backend_windows.cpp)
else()
    list(APPEND LIB_SOURCES src/lib/pty_backend_posix.cpp)
endif()
```

Factory: `PtyBackend::Create(wxEvtHandler*)` returns
`std::unique_ptr<PtyBackend>`.

### Threading Model and Output Buffering

- A PTY backend runs an I/O thread that reads the output of the child process.
- The `on_output` callback runs on that thread and calls
  `wxTerminalViewCtrl::Feed()`, which only appends to `m_feedBuffer`.
- A `wxTimer` (`m_feedTimer`, started by `WakeFeedTimer()`) drains the buffer on
  the GUI thread, at most `kMaxFeedBytesPerTick` bytes per tick, so heavy output
  cannot block the UI. The timer stops itself when the buffer is empty.
- When the user scrolls back, the viewport stays where it is while new output
  arrives.

### Keyboard Input Handling

- `wxEVT_CHAR_HOOK` catches Enter, Tab, and Escape before default navigation.
  `wxEVT_KEY_DOWN` handles Ctrl, Cmd, and Alt combinations plus special keys
  (arrows, Home, function keys). `wxEVT_CHAR` (`OnChar()`) sends characters
  above ASCII.
- Cursor keys are sent in SS3 form (`ESC O A`) when DECCKM is set, and in CSI
  form (`ESC [ A`) otherwise.
- **Keyboard layouts:** the key code of a `wxEVT_KEY_DOWN` event is layout
  independent. With a non-Latin layout (Hebrew, Russian, ...) it holds the Latin
  letter of the physical key, so it must never be used as text.
  - macOS: `OnCharHook()` calls `terminal::TranslateKeyWithActiveLayout()`
    (`src/lib/keyboard_layout_mac.cpp`, `UCKeyTranslate` with the active
    layout). It sends the real character and consumes the event. When the layout
    maps the key to nothing (for example Shift plus a letter key in Hebrew) it
    sends nothing and still consumes the event, which stops the macOS bell. Dead
    keys work because the dead-key state is kept between calls.
  - Linux/GTK: the character arrives in `wxEVT_CHAR` and `OnChar()` sends it.
  - Windows: still open. wxMSW reports the Latin virtual key in both
    `GetKeyCode()` and `GetUnicodeKey()`, so a non-Latin layout sends the wrong
    letter. The fix would be a `keyboard_layout_msw.cpp` using `ToUnicodeEx()`.
- `GetUnicodeKey()` is not usable in `wxEVT_KEY_DOWN` on Windows, so Shift is
  handled there with a `shiftMap` table.
- `AcceptsFocus()` and `AcceptsFocusFromKeyboard()` return `true`.

### Selection System

Two selection mechanisms exist side by side:

- **Mouse selection**: `LinearSelection m_mouseSelection`, with anchor and
  current point in viewport coordinates
- **API selection**: `ApiSelection m_userSelection` for selection from code
- Both are drawn with the theme colors `selectionBg` and `selectionFg`

## Build Configuration

### Build with build.sh (recommended)

`build.sh` in the repository root builds wxWidgets and then GlyphT with one
command. It works on Windows (MSYS2), macOS and Linux. Run it from the
repository root:

```bash
./build.sh              # build wxWidgets (first run only), then glypht
./build.sh --cmake      # same, but force the CMake configure step
./build.sh clean        # make clean
./build.sh package      # build, then create the package for this OS
./build.sh distclean    # delete .build-release, including wxWidgets
./build.sh --help       # show the usage text
```

What it does:

- Everything goes into `.build-release/`. The script always builds `Release`.
- It needs these tools in `PATH`: `cmake`, `git`, `make`, and a compiler
  (`g++` on Linux, `clang++` on macOS and Windows). It stops if one is missing.
- wxWidgets (default `v3.3.4`) is cloned into `.build-release/wxWidgets` and
  installed into `.build-release/wxWidgets-install`. This happens only once.
- CMake runs again only when `.build-release/CMakeCache.txt` is missing, when
  `CMakeLists.txt` is newer than the `Makefile`, or when you pass `--cmake`.
- On Windows, run it in the MSYS2 **CLANG64** shell. The script then sets
  `WXWIN` for you.

Where the result is:

| Platform | Result |
|----------|--------|
| Windows | `.build-release/install/bin/glypht.exe` (DLLs and `themes/` are next to it) |
| macOS | `.build-release/glypht.app` |
| Linux | `.build-release/glypht` |

Environment variables:

| Variable | Default | Meaning |
|----------|---------|---------|
| `WX_VERSION` | `v3.3.4` | wxWidgets tag to build |
| `WX_CONFIG` | (none) | Use this `wx-config` and skip the wxWidgets build (macOS, Linux) |
| `USE_OPENGL` | `1` on Windows and macOS, `0` on Linux | `0` or `1`, picks the renderer |
| `MACOS_DEPLOYMENT_TARGET` | `13.0` | macOS deployment target |
| `GLYPHT_VERSION` | `1.0.0` | Version in the package name (`package` target) |

Notes:

- The script skips the wxWidgets build when it finds an install already. If you
  change `WX_VERSION`, run `./build.sh distclean` first. On Windows an old
  `wxmsw*.dll` can stay in `install/bin` after a version change.
- The script has no debug mode. For a debug build use the manual steps below.
- `./build.sh package` builds first, then makes one package in `.build-release/`:
  - Windows: `glypht-<version>-amd64-installer.exe`, an Inno Setup installer.
    It needs Inno Setup 6 (`ISCC.exe`). If CMake did not find it, the script
    stops with an error; install it and run `./build.sh --cmake package`.
  - macOS: `glypht-<version>-macos-<arch>.zip` with `glypht.app` (not signed).
  - Linux: `glypht-<version>-linux-<arch>.tar.gz` with `glypht`, `glypht.svg`
    and `themes/`.
  - On macOS and Linux the package step is not tested yet. Set the version with
    `GLYPHT_VERSION=1.2.3 ./build.sh package`.
- Build log lines look like `INFO ...` and `ERROR ...`. When a build fails, read
  the lines above the first `ERROR`.

### Manual CMake builds (for example Debug)

Use these when you need a Debug build or a build tree of your own. wxWidgets
must already be installed.

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_GLYPHT` | `ON` | Build the GlyphT demo application |
| `USE_OPENGL` | `ON` on Windows and macOS, `OFF` on Linux | Use the OpenGL renderer. `ON` on Linux stops the configure step with an error. Can also be set with the `USE_OPENGL` environment variable. |
| `WXWIN` | (required on Windows) | wxWidgets install prefix |

### Windows (MinGW) Build

```bash
cd .build-debug
cmake .. -DWXWIN=<wxWidgets install prefix> -DCMAKE_BUILD_TYPE=Debug
make -j32
```

### Windows Installer

`cmake --build . --target setup` builds an Inno Setup installer. It needs
`ISCC.exe` (Inno Setup 6). `cmake/StageInstaller.cmake` copies `install/bin`
into `<build>/installer/` and skips old `wxmsw*.dll` files. `installer.iss`
is generated from `installer.iss.in`. The version comes from `GLYPHT_VERSION`.
The installer has no admin rights and installs per user.

### Linux/macOS Build

```bash
cd .build-debug
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j32
```

On macOS the library links `CoreText`, `CoreGraphics`, and `Carbon` (the last
one for keyboard layout translation).

## Custom Events

| Event | Fired When | Handler Signature |
|-------|-----------|-------------------|
| `wxEVT_TERMINAL_TITLE_CHANGED` | OSC 0/2 title sequence | `evt.GetTitle()` |
| `wxEVT_TERMINAL_CWD_CHANGED` | OSC 7 or OSC 9;9 current directory | `evt.GetString()` (decoded path); also `GetCurrentDir()` |
| `wxEVT_TERMINAL_TERMINATED` | Child process exits | No payload |
| `wxEVT_TERMINAL_TEXT_LINK` | Ctrl+click on text | `evt.GetClickedText()` |
| `wxEVT_TERMINAL_BELL` | BEL character received | No payload |
| `wxEVT_TERMINAL_APP_OWNS_MOUSE` | Program turns mouse tracking on (DECSET 9/1000/1002/1003) | No payload |
| `wxEVT_TERMINAL_APP_RELEASE_MOUSE` | Program turns mouse tracking off, or the terminal is reset | No payload |
| `wxEVT_TERMINAL_PROGRESS` | Program sets or clears a progress bar with OSC 9;4 | `evt.GetProgressState()` (`wxTerminalProgressState`), `evt.GetProgressValue()` (0-100), `evt.ShowProgress()` / `evt.HideProgress()` |
| `wxEVT_TERMINAL_APP_SELECTION_CHANGED` | Program copies text with OSC 52, or the text is cleared | `evt.GetString()` (empty after a clear); also `GetAppSelection()` / `ClearAppSelection()` |

## Common Gotchas

1. **GUI testing**: this is a GUI application. Agents cannot use it. Always ask
   the user to test.

2. **Windows ConPTY version**: needs Windows 10 build 17763 or newer. The code
   checks at runtime whether the API is there.

3. **Resize behavior**: `TerminalCore::Resize()` keeps the content. It once
   called `Reset()`, which cleared everything. That was fixed.

4. **Backspace**: sends `0x7F` (DEL), not `0x08` (BS), because that is what
   cmd.exe and common shells expect.

5. **Line feed**: `\n` (LF) only moves the cursor down; `\r` (CR) moves it to
   column 0. `\r\n` together gives "next line".

6. **Font caching**: `UpdateFontCache()` builds the cached font variants (bold,
   underlined). Call it after a theme or font change.

7. **Safe drawing mode**: `EnableSafeDrawing(true)` switches the wxDC path to
   per-cell drawing. It has no effect on the OpenGL path.

8. **Wide characters (CJK, emoji)**: a wide character occupies two or more
   columns. The cells after its first cell are **fillers** (`Cell::width == 0`)
   owned by it: they have no glyph and share its attributes (`screen_write()` in
   the vendored libtsm sets this). `PrepareRowForDrawing()` reports such a row
   with `row_has_wide`, and `RenderRowWithGrouping()` then draws it with
   `RenderRowNoGrouping()`, which paints all cell backgrounds first and the
   glyphs afterwards. Do not draw a wide row as a run of text and never paint a
   cell background after a glyph: both cut the wide glyph in half. The block
   cursor spans the columns of a wide character (`CursorCellAt()` also moves a
   cursor on a filler cell back to its owner). libtsm's `screen_write()` blanks
   the other half when a write lands on one half of a wide character. Use
   `tsm_wcwidth(uint32_t)`, not `wcwidth(wchar_t)`: `wchar_t` is 16 bits on
   Windows. In the OpenGL renderer on Windows, emoji are drawn with Direct2D
   (`DrawColourGlyphD2D()` in `terminal_gl_renderer.cpp`) to get colour; it was
   checked with wxWidgets v3.3.4.

9. **Header and footer strips**: `AddHeader()` and `AddFooter()` reserve a strip
   above and below the grid (both are a `Strip`). All pixel to cell conversions
   must go through `HeaderHeight()` and `FooterHeight()`: `GetTerminalRect()` is
   between the strips, `PointToCell()` and `ClampToGrid()` subtract the header
   (and ignore the footer), and both renderers start their rows (and the
   cursor) at `HeaderHeight()`. A strip is drawn by `DrawStrip()` (wxDC) and
   `DrawStripGL()`, from `LayoutStrip()`.

10. **CI option name is stale**: the workflows still pass
   `-DBUILD_WXTERMINAL_DEMO=OFF`. That option no longer exists; the current name
   is `BUILD_GLYPHT`, so that CI job builds the demo anyway.

## Integration Example

```cpp
#include "terminal_view.h"

// Create a terminal that runs the default shell
wxTerminalViewCtrl* term = new wxTerminalViewCtrl(parent, "", std::nullopt);
term->SetTheme(wxTerminalTheme::MakeDarkTheme());

// Handle title changes
term->Bind(wxEVT_TERMINAL_TITLE_CHANGED, [](wxTerminalEvent& evt) {
    frame->SetTitle(evt.GetTitle());
});

// Send input
term->SendCommand("ls -la");  // Sends the text plus Enter
```

## Custom Instructions

<!-- This section is maintained by developers and agents during day-to-day work.
     It is NOT auto-generated by codebase-summary and MUST be preserved during
     refreshes. Add project-specific conventions, gotchas, and workflow
     requirements here. -->
