# Key Processes and Workflows

## Data Flow Overview

```mermaid
graph TB
    subgraph "Input Path"
        USER[User] -->|Keyboard/Mouse| VIEW[wxTerminalViewCtrl]
        VIEW -->|SendInput| PTY[PtyBackend]
        PTY -->|Write| PROC[Child Process]
    end
    
    subgraph "Output Path"
        PROC -->|stdout/stderr| IO[I/O Thread]
        IO -->|on_output| PTY
        PTY -->|Feed| VIEW
        VIEW -->|PutData| CORE[TerminalCore]
        CORE -->|Buffer Update| RENDER[Render]
    end
    
    subgraph "Render Path"
        RENDER -->|Paint Event| DC[wxDC/wxGCDC]
        DC -->|Display| SCREEN[Screen]
    end
```

---

## 1. Terminal Initialization Workflow

```mermaid
sequenceDiagram
    participant APP as Application
    participant VIEW as wxTerminalViewCtrl
    participant CORE as TerminalCore
    participant PTY as PtyBackend
    participant PROC as Child Process

    APP->>VIEW: new wxTerminalViewCtrl(parent, command, env)
    VIEW->>CORE: new TerminalCore(rows, cols, maxLines)
    VIEW->>PTY: PtyBackend::Create(this)
    PTY-->>VIEW: backend instance
    VIEW->>PTY: Start(command, env, on_output)
    PTY->>PROC: Launch process
    PTY-->>VIEW: true (success)
    VIEW->>VIEW: SetTerminalSizeFromClient()
    VIEW->>CORE: Resize(rows, cols)
    VIEW->>PTY: Resize(cols, rows)
```

---

## 2. Input Handling Workflow

### Keyboard Input

```mermaid
sequenceDiagram
    participant USER as User
    participant VIEW as wxTerminalViewCtrl
    participant PTY as PtyBackend
    participant PROC as Child Process

    USER->>VIEW: Key press
    VIEW->>VIEW: OnCharHook(event)
    
    alt Special Key
        VIEW->>VIEW: HandleSpecialKeys(event)
        VIEW->>VIEW: SendInput(escape_sequence)
    else Printable Character
        VIEW->>VIEW: OnKeyDown(event)
        VIEW->>VIEW: Process character with Shift state
        VIEW->>VIEW: SendInput(character)
    else Ctrl+Combination
        VIEW->>VIEW: Send control character (0x01-0x1A)
    end
    
    VIEW->>PTY: Write(data)
    PTY->>PROC: stdin
```

### Mouse Input

```mermaid
sequenceDiagram
    participant USER as User
    participant VIEW as wxTerminalViewCtrl
    participant CORE as TerminalCore

    alt Left Click
        USER->>VIEW: OnMouseLeftDown()
        VIEW->>VIEW: SetFocus()
        VIEW->>VIEW: Start selection
    else Left Double Click
        USER->>VIEW: OnMouseLeftDoubleClick()
        VIEW->>VIEW: SelectionRectFromMousePoint()
        VIEW->>CORE: SetClickedRange(rect)
    else Mouse Move (with button down)
        USER->>VIEW: OnMouseMove()
        VIEW->>VIEW: Update selection end
        VIEW->>VIEW: ScrollViewportForSelection()
    else Mouse Up
        USER->>VIEW: OnMouseUp()
        VIEW->>VIEW: Finalize selection
    else Right Click
        USER->>VIEW: OnContextMenu()
        VIEW->>VIEW: Show context menu (Copy/Paste)
    else Mouse Wheel
        USER->>VIEW: OnMouseWheel()
        VIEW->>VIEW: Adjust viewStart
        VIEW->>VIEW: UpdateScrollbar()
    end
```

### Ctrl+Click (Link Detection)

```mermaid
sequenceDiagram
    participant USER as User
    participant VIEW as wxTerminalViewCtrl
    participant CORE as TerminalCore
    participant EVT as wxTerminalEvent

    USER->>VIEW: Ctrl+Click
    VIEW->>VIEW: DoClickable(event, true)
    VIEW->>VIEW: SelectionRectFromMousePoint()
    VIEW->>CORE: SetClickedRange(rect)
    VIEW->>CORE: GetClickedText()
    CORE-->>VIEW: clicked text
    VIEW->>EVT: wxEVT_TERMINAL_TEXT_LINK
    EVT-->>APP: Application handles link
```

---

## 3. Escape Sequence Parsing Workflow

```mermaid
graph TD
    A[PutData] -->|character by character| B{In Escape?}
    B -->|No| C{Is ESC?}
    C -->|Yes| D[Set m_inEscape = true]
    C -->|No| E{Is Printable?}
    E -->|Yes| F[PutPrintable]
    E -->|No| G[Handle Control Char]
    
    B -->|Yes| H[Accumulate in m_escape]
    H --> I{Complete Sequence?}
    I -->|Yes| J[ParseEscape]
    I -->|No| K[Continue accumulating]
    
    J -->|CSI [ ... | L[Parse CSI params]
    J -->|OSC ] ... | M[Parse OSC string]
    J -->|Other| N[Handle single-char escape]
    
    L --> O[Execute CSI command]
    M --> P[Execute OSC command]
```

### Supported Escape Sequences

#### CSI Sequences (ESC[...)
| Sequence | Description |
|----------|-------------|
| `ESC[nA` | Cursor up |
| `ESC[nB` | Cursor down |
| `ESC[nC` | Cursor forward |
| `ESC[nD` | Cursor back |
| `ESC[nE` | Cursor next line |
| `ESC[nF` | Cursor previous line |
| `ESC[nG` | Cursor horizontal absolute |
| `ESC[row;colH` / `ESC[row;colf` | Cursor position |
| `ESC[nJ` | Erase display (0=after, 1=before, 2=all, 3=scrollback) |
| `ESC[nK` | Erase line (0=after, 1=before, 2=all) |
| `ESC[nS` | Scroll up |
| `ESC[nT` | Scroll down |
| `ESC[nX` | Erase character |
| `ESC[n@` | Insert character |
| `ESC[nP` | Delete character |
| `ESC[nL` | Insert line |
| `ESC[nM` | Delete line |
| `ESC[nm` | SGR (Select Graphic Rendition) |
| `ESC[?nh` / `ESC[?nl` | Set/reset mode |
| `ESC[s` | Save cursor |
| `ESC[u` | Restore cursor |

#### OSC Sequences (ESC]...)
| Sequence | Description |
|----------|-------------|
| `ESC]0;titleBEL` | Set icon and window title |
| `ESC]2;titleBEL` | Set window title |

#### SGR Parameters
| Parameter | Effect |
|-----------|--------|
| 0 | Reset |
| 1 | Bold |
| 4 | Underline |
| 7 | Reverse video |
| 30-37 | Foreground color (ANSI) |
| 40-47 | Background color (ANSI) |
| 90-97 | Bright foreground |
| 100-107 | Bright background |
| 38;5;n | Foreground 256-color |
| 48;5;n | Background 256-color |
| 38;2;r;g;b | Foreground true color |
| 48;2;r;g;b | Background true color |

---

## 4. Rendering Pipeline

```mermaid
sequenceDiagram
    participant TIMER as Timer/Refresh
    participant VIEW as wxTerminalViewCtrl
    participant CORE as TerminalCore
    participant DC as wxDC/wxGCDC

    TIMER->>VIEW: OnPaint event
    VIEW->>VIEW: wxBufferedPaintDC
    VIEW->>CORE: GetViewArea()
    CORE-->>VIEW: visible rows
    
    loop For each visible row
        VIEW->>VIEW: RenderRow()
        
        alt Grouping mode
            VIEW->>VIEW: RenderRowWithGrouping()
            VIEW->>VIEW: Group adjacent cells with same attrs
            VIEW->>DC: Draw grouped text/rectangles
        else No grouping mode
            VIEW->>VIEW: RenderRowNoGrouping()
            VIEW->>DC: Draw cell by cell
        end
        
        alt POSIX mode
            VIEW->>VIEW: RenderRowPosix()
        end
    end
    
    VIEW->>VIEW: Draw cursor
    VIEW->>VIEW: Draw focus border
```

### Rendering Strategies

1. **Row Grouping** (`RenderRowWithGrouping`): Groups adjacent cells with identical attributes for efficient `DrawText` calls
2. **No Grouping** (`RenderRowNoGrouping`): Cell-by-cell rendering for maximum compatibility
3. **POSIX Optimized** (`RenderRowPosix`): Platform-specific optimizations

---

## 5. Terminal Resize Workflow

```mermaid
sequenceDiagram
    participant USER as User/Window Manager
    participant VIEW as wxTerminalViewCtrl
    participant CORE as TerminalCore
    participant PTY as PtyBackend

    USER->>VIEW: Window resize
    VIEW->>VIEW: OnSize(event)
    VIEW->>VIEW: SetTerminalSizeFromClient()
    VIEW->>VIEW: Calculate new rows/cols from pixel size
    VIEW->>CORE: Resize(rows, cols)
    CORE->>CORE: Preserve existing content
    CORE->>CORE: Copy content to new buffer
    CORE->>CORE: Adjust cursor position
    VIEW->>PTY: Resize(cols, rows)
    PTY->>PTY: Platform-specific resize
    VIEW->>VIEW: RefreshView()
```

### Resize Behavior
- Preserves existing screen content when possible
- Copies as much content as fits in new dimensions
- Adjusts cursor to remain within new bounds
- Handles both growing and shrinking
- Notifies PTY backend of size change

---

## 6. Selection and Copy/Paste Workflow

### Selection Creation

```mermaid
sequenceDiagram
    participant USER as User
    participant VIEW as wxTerminalViewCtrl
    participant CORE as TerminalCore

    USER->>VIEW: Mouse down
    VIEW->>VIEW: OnMouseLeftDown()
    VIEW->>VIEW: PointToCell()
    VIEW->>VIEW: Start LinearSelection
    
    USER->>VIEW: Mouse move
    VIEW->>VIEW: OnMouseMove()
    VIEW->>VIEW: Update selection.current
    VIEW->>VIEW: ScrollViewportForSelection() if needed
    VIEW->>VIEW: RefreshView()
    
    USER->>VIEW: Mouse up
    VIEW->>VIEW: OnMouseUp()
    VIEW->>VIEW: Finalize selection
```

### Copy Workflow

```mermaid
sequenceDiagram
    participant USER as User
    participant VIEW as wxTerminalViewCtrl
    participant CORE as TerminalCore
    participant CLIP as Clipboard

    USER->>VIEW: Ctrl+C or Right-click > Copy
    VIEW->>VIEW: Copy()
    VIEW->>CORE: Get text from selection range
    CORE-->>VIEW: selected text
    VIEW->>CLIP: wxClipboard::SetData()
```

### Paste Workflow

```mermaid
sequenceDiagram
    participant USER as User
    participant VIEW as wxTerminalViewCtrl
    participant CLIP as Clipboard
    participant PTY as PtyBackend

    USER->>VIEW: Ctrl+V or Right-click > Paste
    VIEW->>VIEW: Paste()
    VIEW->>CLIP: wxClipboard::GetData()
    CLIP-->>VIEW: clipboard text
    VIEW->>PTY: Write(text)
```

---

## 7. Process Lifecycle Workflow

```mermaid
sequenceDiagram
    participant APP as Application
    participant VIEW as wxTerminalViewCtrl
    participant PTY as PtyBackend
    participant PROC as Child Process

    APP->>VIEW: Create terminal
    VIEW->>PTY: Start(command, env, callback)
    PTY->>PROC: Launch child process
    
    loop Process Running
        PROC->>PTY: Output data
        PTY->>VIEW: on_output callback
        VIEW->>VIEW: Feed(data) → TerminalCore
    end
    
    PROC->>PTY: Process exits
    PTY->>VIEW: wxEVT_TERMINAL_TERMINATED
    VIEW->>APP: Event notification
    
    alt Restart
        APP->>VIEW: Create new terminal
    else Close
        APP->>VIEW: Destroy
        VIEW->>PTY: Stop()
        PTY->>PTY: Cleanup resources
    end
```

---

## 8. Theme Application Workflow

```mermaid
sequenceDiagram
    participant USER as User
    participant APP as MyFrame
    participant VIEW as wxTerminalViewCtrl
    participant CORE as TerminalCore

    USER->>APP: Select theme (Dark/Light)
    APP->>APP: ApplyNativeAppTheme()
    APP->>APP: MakeDarkTheme() / MakeLightTheme()
    APP->>VIEW: SetTheme(theme)
    VIEW->>CORE: SetTheme(theme)
    VIEW->>VIEW: UpdateFontCache()
    VIEW->>VIEW: Refresh()
```
