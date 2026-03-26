# Terminal Emulator Development Summary

## Project Overview
Developing an embedded terminal emulator as a wxPanel in C++ on Windows using ConPTY. Project location is where this file located.

**Build commands:**

Windows:

```bash
cd .build-debug
cmake -DCMAKE_BUILD_TYPE=Debug -DWXWIN=C:/msys64/home/eran/root ..  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
make -j32
```

Linux:

```bash
cd .build-debug
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
make -j32
```

## GUIDELINES AND RULES

- In order to test the application, ALWAYS ask the user to do it for you. This is a GUI program which you can not interact with.

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

### 3. Interactive Input (terminal_view.cpp)
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
- ✅ Ctrl combinations (Ctrl+C for SIGINT/copy, Ctrl+V for paste)
- ✅ Ctrl+L to clear screen
- ✅ Ctrl+U to clear current line
- ✅ Full ANSI color support (16/256/RGB)
- ✅ Text attributes (bold, underline, reverse)
- ✅ Cursor movement and positioning
- ✅ Escape sequence parsing (mostly complete)
- ✅ Screen clearing commands (cls now works correctly)
- ✅ Terminal resize with content preservation
- ✅ Mouse selection and copy/paste functionality
- ✅ Backspace fixed to delete single character (sends 0x7F instead of 0x08)
- ✅ OSC sequence handling fixed (window titles no longer bleed into output)
- ✅ Window title updates via OSC 0/2 sequences (fires wxTerminalEvent)
- ✅ Debug logging removed - production ready code
- ✅ Command history with Up/Down arrows
- ✅ Common keyboard shortcuts (Ctrl+L, Ctrl+U)

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

### 4. Special Character Input with Shift (terminal_panel.cpp)
**Problem**: When typing special characters with Shift modifier (e.g., `"` by pressing Shift+'), the terminal was printing the wrong character (e.g., `'` instead of `"`).

**Root Cause**: The keyboard event handler was sending the raw key code without considering Shift state for special characters.

**Fix**:
- Created a comprehensive `shiftMap` (std::unordered_map<int, int>) that maps key codes to their Shift-modified values
- Map includes:
  - **Numbers**: `0-9` → `)!@#$%^&*(`
  - **Special characters**:
    - `'` → `"`
    - `/` → `?`
    - `;` → `:`
    - `[` → `{`, `]` → `}`
    - `\` → `|`
    - `,` → `<`, `.` → `>`
    - `-` → `_`, `=` → `+`
    - `` ` `` → `~`
- Updated `OnKeyDown` to look up keys in the shift map and send the appropriate character based on Shift state
- This provides a clean, maintainable solution for all Shift-modified characters

### 5. Pager/Interactive Application Support (terminal_panel.cpp)
**Problem**: Commands like `git log`, `less`, `more` that use pagers appeared to not be responding to input (e.g., pressing 'q' to quit).

**Root Cause**: There were two event handlers processing keyboard input:
- `OnKeyDown` - handling and sending characters
- `OnChar` - also trying to send the same characters

This created a conflict where characters were potentially being double-sent or interfering with each other.

**Fix**:
- Disabled character processing in `OnChar` event handler
- All keyboard input is now handled exclusively in `OnKeyDown`
- This ensures single, clean character transmission to the terminal

**Status**: ✅ **WORKING** - See issue #6 below for the actual fix.

### 6. Escape Sequence Parsing Bug - Single-Byte Sequences (terminal_core.cpp)
**Problem**: Commands like `git log` would display but couldn't be exited. Pressing `q` appeared to do nothing, and the screen stayed locked on the pager view.

**Root Cause**:
- The pager was sending `ESC>` (Normal Keypad Mode) followed by `ESC[K` (Erase Line)
- `ESC>` (`>` = 0x3E) was not being recognized as a valid escape sequence terminator (range was 0x40-0x7E only)
- This left the escape buffer in a corrupted state with unclosed sequence data
- When `ESC[K` arrived, it was appended to the corrupted buffer and never parsed
- The erase command never executed, leaving the `:` prompt and git log content on screen
- Subsequent screen updates couldn't clear the old content

**Fix**:
- Extended escape sequence terminator detection to include range 0x30-0x3F
- This covers single-byte escape sequences like:
  - `ESC>` (0x3E) - Normal Keypad Mode
  - `ESC=` (0x3D) - Application Keypad Mode
  - `ESC7` (0x37) - Save Cursor (DECSC)
  - `ESC8` (0x38) - Restore Cursor (DECRC)
- These sequences are now properly recognized and terminated
- Prevents escape buffer corruption
- `ESC[K` and other CSI sequences now parse correctly

**Result**: ✅ Pagers (git log, less, more) now work perfectly. Press `q` to quit, Space to scroll, arrows to navigate.

## Recent Changes - Command History Implementation

### Feature Added
**Command History Navigation**: Users can now press Up/Down arrow keys to cycle through previously executed commands.

### How It Works
1. **Command Tracking**: The terminal tracks every character typed by the user
2. **History Storage**: When Enter is pressed, the command is saved to history (duplicates are not added consecutively)
3. **Navigation**:
   - Press **Up Arrow** to go back through command history
   - Press **Down Arrow** to go forward through history
   - Pressing Down at the end clears the line
4. **Editing**: Any character typed or deleted resets the history index back to current input
5. **Line Replacement**: When navigating history, the current line is cleared and replaced with the history item

### Changes Made
1. **terminal_panel.h**:
   - Added `std::vector<std::string> m_commandHistory` to store command history
   - Added `int m_historyIndex` to track current position in history (-1 = current command)
   - Added `std::string m_currentCommand` to track what user is currently typing

2. **terminal_panel.cpp**:
   - **OnCharHook()**: Added Up/Down arrow handling for history navigation with line clearing/replacement logic
   - **OnCharHook()**: Enter key now saves command to history before submitting
   - **OnKeyDown()**: All character input (letters, numbers, special chars) now tracked in `m_currentCommand`
   - **OnKeyDown()**: Backspace removes last character from `m_currentCommand`
   - **OnKeyDown()**: Ctrl+C clears current command
   - **OnKeyDown()**: Up/Down arrows no longer send escape sequences to terminal (handled by history instead)
   - **OnPaste()**: Pasted text is added to `m_currentCommand`

### Testing
1. Type a command like `echo hello` and press Enter
2. Type another command like `dir` and press Enter
3. Press Up arrow - should show `dir`
4. Press Up arrow again - should show `echo hello`
5. Press Down arrow - should show `dir`
6. Press Down arrow again - should clear the line

### Important Notes

**This is application-level command history**, which means:
- ✅ **Works independently** of shell's built-in history
- ✅ **Intercepts keypresses** before sending to ConPTY
- ✅ **Tracks everything** you type in real-time
- ✅ **Clears and replaces** the current line when navigating
- ❌ **Not persistent** - history is lost when application closes
- ❌ **Only for this session** - doesn't interact with shell history files

**Why not use shell's built-in history?**
- We're working at the PTY level, passing raw keypresses
- Shell history (cmd.exe/PowerShell) would conflict with our tracking
- Application-level gives us more control and consistent behavior
- Future enhancement: Could save history to file for persistence

## Recent Changes - Common Keyboard Shortcuts

### Features Added
Added support for common Unix-style keyboard shortcuts that are standard in most terminal emulators.

### Shortcuts Implemented

1. **Ctrl+U - Clear Current Line**
   - Clears everything typed on the current line (line kill)
   - Sends backspace sequences to erase the current command
   - Clears the internal `m_currentCommand` buffer
   - Resets history navigation index
   - Standard behavior in Unix shells (bash, zsh, etc.)

### Changes Made
**terminal_panel.cpp - OnKeyDown()**:
- Added Ctrl+L handler before the general Ctrl+A-Z handler
- Added Ctrl+U handler before the general Ctrl+A-Z handler
- Ctrl+L implementation:
  - Sends `\x1b[2J\x1b[H` to the shell (proper ANSI sequences)
  - Sequences are parsed by terminal core, keeping cursor position synced
  - Forces refresh to update display
- Ctrl+U implementation:
  - Generates backspace sequences to erase current line
  - Clears `m_currentCommand` buffer
- Both handlers return early to prevent falling through to generic Ctrl key handling

### Bug Fixes
1. **Initial Issue**: Sending `\f` (form feed) displayed as `^L` on screen
   - **Fix**: Changed to ANSI escape sequences `\x1b[2J\x1b[H`

2. **Cursor Position Issue**: After clearing, cursor remained at previous location
   - **Root Cause**: Mixing direct manipulation (`m_core.ClearScreen()`) with escape sequences
   - **Fix**: Removed direct manipulation, only send escape sequences to shell
   - Escape sequences are processed normally through terminal core
   - This keeps shell cursor position and display cursor position synchronized

### Testing
- **Ctrl+L**: Type some commands, then press Ctrl+L → screen should clear and cursor at top-left
- **Ctrl+U**: Type a partial command like `echo hello`, press Ctrl+U → line should clear completely

## Keyboard Shortcuts Reference

### Editing & Navigation
- **Backspace** - Delete previous character
- **Delete** - Delete character at cursor
- **Left/Right Arrows** - Move cursor
- **Home** - Move to beginning of line
- **End** - Move to end of line
- **Tab** - Command completion (shell-dependent)

### Command History
- **Up Arrow** - Previous command in history
- **Down Arrow** - Next command in history

### Line & Screen Control
- **Ctrl+C** - Copy selected text (if selection exists), otherwise send SIGINT
- **Ctrl+V** - Paste from clipboard
- **Ctrl+L** - Clear screen
- **Ctrl+U** - Clear current line
- **Ctrl+A through Z** - Send control characters (standard terminal behavior)

### Function Keys
- **F1-F12** - Function keys (application-dependent)

## Quick Test Plan for New Features

### Test Command History
```
1. Type: echo test1 [Enter]
2. Type: echo test2 [Enter]
3. Press Up → should show "echo test2"
4. Press Up → should show "echo test1"
5. Press Down → should show "echo test2"
6. Press Down → should clear line
```

### Test Ctrl+L (Clear Screen)
```
1. Run several commands (dir, echo hello, etc.)
2. Press Ctrl+L
3. Screen should clear completely
```

### Test Ctrl+U (Clear Line)
```
1. Type: echo this is a test
2. Press Ctrl+U (without pressing Enter)
3. The line should be cleared
4. Type a new command and press Enter - it should work normally
```

## Known Issues to Address
None currently!

## Next Steps / TODO List

### Completed
1. ~~**Remove Debug Logging**~~ ✅
2. ~~**Mouse Wheel Scrollback**~~ ✅
3. ~~**Scroll region + Insert/Delete Lines**~~ ✅
4. ~~**Alternate screen buffer**~~ ✅ (`?1049h`/`?1049l`, `?47h`/`?47l`)
5. ~~**Cursor show/hide**~~ ✅ (`?25h`/`?25l` acknowledged)
6. ~~**Reverse Index / Index / Next Line**~~ ✅ (`ESCM`, `ESCD`, `ESCE`)
7. ~~**ESC7/ESC8 save/restore cursor**~~ ✅
8. ~~**Intermediate byte sequences**~~ ✅ (`ESC(B`, `ESC)0`, etc.)
9. ~~**Auto-wrap line wrapping**~~ ✅ (cursor resets to col 0 on wrap)
10. ~~**Theme support**~~ ✅ (`wxTerminalTheme` with named wxColour fields, dark/light themes)
11. ~~**Public API**~~ ✅ (ScrollToLastLine, GetLineCount, SetBufferSize, GetBufferSize, CenterLine, GetLine, SetSelection, ClearSelection, SetTheme)

### Bug Fixes
- **Scrollback buffer not growing**: `NewLine()` was calling `ScrollRegionUp()` even for full-screen scroll regions, which shifts rows in-place instead of growing the buffer. Fixed to use `ScrollUp()` when scroll region covers the full screen.
- **Escape parser stuck on `ESC(B`**: Intermediate bytes (0x20-0x2F) weren't handled, causing the parser to never terminate sequences like `ESC(B` (Select Character Set). This caused `top` to hang.
- **Single-byte escape double-push**: Characters were pushed into the escape buffer twice, so `ESCM` was parsed as `"MM"` instead of `"M"`. Fixed by removing the redundant push.

### High Priority - Escape Sequences

#### Currently Handled
- Cursor movement: A, B, C, D, E, F, G, H, f, d
- Erase: J (modes 0/1/2/3), K (modes 0/1/2), X
- Scroll: S (up), T (down), scroll regions (r/DECSTBM)
- Line editing: P (delete chars), @ (insert blanks), L (insert lines), M (delete lines)
- Cursor: s/u (save/restore), ESC7/ESC8 (DEC save/restore)
- Repeat: b (repeat last char)
- SGR (m): full 16/256/truecolor, bold, underline, reverse
- Device status: n (cursor position report), c (device attributes)
- OSC: window title (0, 2)
- Single-byte: ESCM (reverse index), ESCD (index), ESCE (next line), ESC>/ESC=
- Intermediate byte sequences: ESC(B, ESC)0, etc.
- Private mode: ?1049h/?1049l (alternate screen buffer), ?25h/?25l, ?1h/?1l, ?7h/?7l
- Escape buffer overflow protection (resets after 256 bytes)

#### Verified Working
- `man ls` — paging with spacebar
- `vi` on large files — scrolling, ESC → :q! and :wq
- `top` — real-time process list, `q` to quit
- Scrollback — mouse wheel scrolling through history

### Medium Priority
3. **Scrollback Navigation (remaining)**
   - Shift+PageUp/PageDown keyboard shortcuts
   - Display scrollbar when scrolled up
   - Show indicator when not at bottom

4. **Font Size Control** ⏱️ ~10 mins
   - Ctrl+Plus/Ctrl+= to increase font size
   - Ctrl+Minus to decrease font size
   - Ctrl+0 to reset to default

### Low Priority / Nice to Have
5. **Selection Improvements**
   - Double-click to select word
   - Triple-click to select entire line
   - Shift+Arrow keys for keyboard selection

6. **Persistent Command History**
   - Save history to file so it survives app restart

7. **Test Complex Applications**
   - Test with htop, nano, tmux
   - Fix any escape sequence gaps

## Files Modified
1. `pty_backend_windows.cpp` - ConPTY handling, ReadFile fix, error diagnostics
2. `terminal_core.cpp` - Escape sequence parser, color support, erase operations, OSC handling, line feed fix
3. `terminal_panel.cpp` - Event handling (Bind API), keyboard input with shift map, rendering, copy/paste, backspace fix
4. `terminal_panel.h` - Event handler declarations, AcceptsFocus overrides
5. `main.cpp` - Removed confusing initial message
