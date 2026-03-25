# Terminal Emulator Development Summary

## Project Overview
Developing an embedded terminal emulator as a wxPanel in C++ on Windows using ConPTY. Project location: `C:\msys64\home\eran\devl\EmbeddedCmdPanel`

**Build commands:**
```bash
cd C:\msys64\home\eran\devl\EmbeddedCmdPanel/.build-debug
cmake -DCMAKE_BUILD_TYPE=Debug -DWXWIN=C:/msys64/home/eran/root ..
make -j32
```

## Initial Problem & Resolution
- **Original Issue**: "Failed to create ConPTY error" - but this was a misdiagnosis
- **Actual Problem**: ConPTY was working fine, but escape sequences weren't being parsed correctly
- **Root Cause**: Terminal was receiving output but displaying raw escape codes instead of interpreting them

## Major Fixes Implemented

### 1. ConPTY Backend (pty_backend_windows.cpp)
- Fixed blocking `ReadFile` by using `PeekNamedPipe` to check for available data first
- Reduced reader thread wait time from 10ms to 1ms for faster I/O
- Added comprehensive error diagnostics with HRESULT codes and GetLastError() values
- Added API availability checks for Windows 10 Build 17763+

### 2. Escape Sequence Parsing (terminal_core.cpp)
- **Enhanced parser to handle**:
  - Private mode CSI sequences (`ESC[?...h/l`)
  - OSC sequences (Operating System Commands)
  - All cursor movement commands (A/B/C/D/E/F/G/H/f)
  - Erase operations (J/K with modes 0/1/2/3)
  - Scroll operations (S)

- **Full color support added**:
  - 16 ANSI colors (30-37, 40-47, 90-97, 100-107)
  - 256-color palette (ESC[38;5;n / ESC[48;5;n)
  - True color RGB (ESC[38;2;r;g;b / ESC[48;2;r;g;b)

- **Critical bug fixed**: '[' and ']' characters were being treated as escape sequence terminators, causing sequences like `ESC[K` to fail. Fixed by excluding '[' and ']' from single-character escape terminator check.

### 3. Interactive Input (terminal_panel.cpp)
- **Migrated from event tables to modern `Bind()` API**
- **Keyboard handling**:
  - Added `wxEVT_CHAR_HOOK` to intercept ENTER/TAB/ESC before default navigation
  - Implemented case-sensitive character input (uppercase issue fixed)
  - Special key support: arrows, function keys (F1-F12), Home/End, Page Up/Down, Insert/Delete
  - Ctrl combinations (Ctrl+A-Z send control characters 0x01-0x1A)
  - Alt combinations (send ESC prefix)

- **Key fixes**:
  - `AcceptsFocus()` and `AcceptsFocusFromKeyboard()` overrides to enable keyboard focus
  - Handle character case conversion manually (Shift detection) since `GetUnicodeKey()` doesn't work properly in KEY_DOWN events on Windows
  - Shift+number keys produce symbols (!@#$%^&*())

### 4. Visual Rendering (terminal_panel.cpp)
- **Per-cell rendering** with individual foreground/background colors
- **Text attributes**: bold, underline, reverse video
- **Blinking cursor** (semi-transparent, 500ms blink rate)
- **Refresh rate**: Increased from 10 FPS to ~60 FPS (16ms timer)

## Current Status - Working Features ✅
- ✅ ConPTY backend fully functional
- ✅ Typing characters (lowercase/uppercase with Shift)
- ✅ ENTER key submits commands
- ✅ Special keys (arrows, backspace, tab, function keys)
- ✅ Ctrl combinations (e.g., Ctrl+C)
- ✅ Full ANSI color support (16/256/RGB)
- ✅ Text attributes (bold, underline, reverse)
- ✅ Cursor movement and positioning
- ✅ Escape sequence parsing (mostly complete)
- ✅ Screen clearing commands (cls now works correctly)
- ✅ Terminal resize with content preservation
- ✅ Mouse selection and copy/paste functionality
- ✅ Backspace fixed to delete single character (sends 0x7F instead of 0x08)
- ✅ OSC sequence handling fixed (window titles no longer bleed into output)
- ✅ Debug logging removed - production ready code

## Recent Changes - Terminal Resize Implementation

### Problem Fixed
- Previously, resizing the terminal window would completely clear the screen content
- The `TerminalCore::Resize()` method was calling `Reset()` which cleared everything

### Changes Made
1. **terminal_core.cpp - Resize() method**:
   - Now preserves existing screen content when resizing
   - Copies as much content as possible from old buffer to new buffer
   - Adjusts cursor position to remain within new bounds
   - Handles both growing and shrinking the terminal size

2. **pty_backend_windows.cpp - Resize() method**:
   - Fixed to check both `ResizePseudoConsole` and `ResizePseudoConsoleDirect` API variants
   - Properly notifies ConPTY backend of size changes

## Recent Changes - Copy/Paste Implementation

### Features Added
1. **Mouse Selection**:
   - Click and drag to select text
   - Selection is highlighted with semi-transparent blue
   - Works across multiple rows and columns
   - Selection persists until cleared by clicking or starting new selection

2. **Copy Functionality**:
   - Right-click menu with "Copy" option (when text is selected)
   - Ctrl+C copies selected text (when text is selected)
   - Ctrl+C without selection sends SIGINT to terminal (standard behavior)
   - Copies text to system clipboard

3. **Paste Functionality**:
   - Right-click menu with "Paste" option
   - Ctrl+V pastes clipboard content to terminal
   - Pasted text is sent to the running process

### Changes Made
1. **terminal_panel.h**: Added selection state tracking and mouse event handlers
2. **terminal_panel.cpp**:
   - Mouse event handlers: OnMouseClick, OnMouseMove, OnMouseUp, OnRightClick
   - Copy/Paste handlers with clipboard integration
   - Updated OnPaint to draw selection highlighting
   - Updated OnKeyDown to intercept Ctrl+C/Ctrl+V

### How to Test Copy/Paste

1. **Start the application** and run a command:
   ```
   dir
   echo This is a test
   ```

2. **Select text with mouse**:
   - Click and drag to select any text
   - Selected text will be highlighted in blue

3. **Copy**:
   - Right-click and choose "Copy", OR
   - Press Ctrl+C

4. **Paste**:
   - Right-click and choose "Paste", OR
   - Press Ctrl+V
   - The pasted text will be sent to the terminal

## Recent Changes - Bug Fixes

### 1. OSC Sequence Handling (terminal_core.cpp)
**Problem**: Window title sequences (OSC) like `ESC]0;title BEL` were being printed as text instead of being consumed.

**Fix**:
- Added proper OSC terminator detection (BEL 0x07 and ST ESC\)
- Separated OSC and CSI sequence handling logic
- OSC sequences now properly consumed and not displayed

### 2. Backspace Key (terminal_panel.cpp)
**Problem**: Pressing backspace was deleting entire words instead of single characters.

**Fix**:
- Changed from sending `\b` (0x08 - BS) to `\x7f` (0x7F - DEL)
- This is the standard terminal behavior that cmd.exe expects
- Now deletes one character at a time correctly

### 3. Line Feed Behavior (terminal_core.cpp)
**Problem**: `\n` was doing both carriage return and line feed, causing cursor positioning issues.

**Fix**:
- Changed `NewLine()` to only move cursor down (standard VT100 behavior)
- `\n` (LF) now only moves down one line
- `\r` (CR) moves to column 0
- `\r\n` together gives expected "next line" behavior

## Known Issues to Address
None currently!

## Next Steps / TODO List

### High Priority
1. ~~**Remove Debug Logging**~~ ✅ **COMPLETED**
   - ~~Remove creation of `.log` files~~
   - ~~Clean up debug code in terminal_panel.cpp and terminal_core.cpp~~
   - ~~Make the codebase production-ready~~

   **What was done:**
   - Removed all `std::ofstream` debug logging from terminal_panel.cpp and terminal_core.cpp
   - Removed `#include <fstream>` as it's no longer needed
   - Removed debug title bar updates
   - Code is now clean and production-ready

### Medium Priority
2. **Scrollback Navigation** ⏱️ ~20 mins
   - Implement mouse wheel scrolling through terminal history
   - Add Shift+PageUp/PageDown keyboard shortcuts
   - Display scrollbar when scrolled up
   - Show indicator when not at bottom

3. **Font Size Control** ⏱️ ~10 mins
   - Ctrl+Plus/Ctrl+= to increase font size
   - Ctrl+Minus to decrease font size
   - Ctrl+0 to reset to default
   - Persist font size preference (optional)

### Low Priority / Nice to Have
4. **Selection Improvements**
   - Double-click to select word
   - Triple-click to select entire line
   - Shift+Arrow keys for keyboard selection

5. **Test Complex Applications**
   - Test with vim, less, top, htop
   - Verify all escape sequences work correctly
   - Fix any issues that arise

## Files Modified
1. `pty_backend_windows.cpp` - ConPTY handling, ReadFile fix, error diagnostics
2. `terminal_core.cpp` - Escape sequence parser, color support, erase operations, OSC handling, line feed fix
3. `terminal_panel.cpp` - Event handling (Bind API), keyboard input, rendering, copy/paste, backspace fix
4. `terminal_panel.h` - Event handler declarations, AcceptsFocus overrides
5. `main.cpp` - Removed confusing initial message
