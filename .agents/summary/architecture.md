# System Architecture

## Overview

wxTerminalEmulator is a cross-platform terminal emulation library built on wxWidgets. It follows a layered architecture with clear separation between terminal emulation logic, platform-specific I/O, and GUI rendering.

## High-Level Architecture

```mermaid
graph TB
    subgraph "Application Layer"
        APP[Demo Application<br/>main.cpp]
    end
    
    subgraph "Presentation Layer"
        VIEW[wxTerminalViewCtrl<br/>terminal_view.h/cpp]
    end
    
    subgraph "Core Layer"
        CORE[TerminalCore<br/>terminal_core.h/cpp]
        EVT[Terminal Events<br/>terminal_event.h/cpp]
        THEME[Terminal Theme<br/>terminal_theme.h]
    end
    
    subgraph "I/O Layer"
        PTY[PtyBackend Interface<br/>pty_backend.h]
        WIN_PTY[WindowsPtyBackend<br/>pty_backend_windows.h/cpp]
        POSIX_PTY[PosixPtyBackend<br/>pty_backend_posix.h/cpp]
    end
    
    subgraph "Platform Layer"
        CONPTY[Windows ConPTY API]
        FORKPTY[POSIX forkpty]
        WX[wxWidgets Framework]
    end
    
    subgraph "Infrastructure"
        LOG[Terminal Logger<br/>terminal_logger.h/cpp]
        PERSIST[App Persistence<br/>app_persistence.h/cpp]
    end
    
    APP --> VIEW
    VIEW --> CORE
    VIEW --> EVT
    VIEW --> THEME
    VIEW --> PTY
    CORE --> THEME
    CORE --> EVT
    PTY --> WIN_PTY
    PTY --> POSIX_PTY
    WIN_PTY --> CONPTY
    POSIX_PTY --> FORKPTY
    VIEW --> WX
    APP --> LOG
    VIEW --> LOG
    CORE --> LOG
    APP --> PERSIST
```

## Design Patterns

### 1. Model-View Pattern
- **Model**: `TerminalCore` manages the terminal state, buffer, and escape sequence parsing
- **View**: `wxTerminalViewCtrl` handles rendering and user input
- The model is independent of wxWidgets; the view bridges to the GUI framework

### 2. Strategy Pattern (Platform Abstraction)
- `PtyBackend` defines the interface for platform-specific pseudo-terminal implementations
- `WindowsPtyBackend` and `PosixPtyBackend` provide concrete implementations
- Selection is determined at compile time via CMake platform checks

### 3. Observer Pattern
- Custom wxWidgets events (`wxTerminalEvent`) notify observers of terminal state changes
- Callbacks (`SetTitleCallback`, `SetResponseCallback`, `SetBellCallback`) allow loose coupling

### 4. Factory Pattern
- `PtyBackend::Create()` factory method instantiates the appropriate backend for the platform
- `wxTerminalTheme::MakeDarkTheme()` and `MakeLightTheme()` create pre-configured themes

## Platform Abstraction Strategy

The codebase uses conditional compilation and separate implementation files for platform-specific code:

```mermaid
graph LR
    A[CMakeLists.txt] -->|WIN32| B[pty_backend_windows.cpp]
    A -->|NOT WIN32| C[pty_backend_posix.cpp]
    B --> D[Windows ConPTY API]
    C --> E[POSIX forkpty]
```

### Platform-Specific Considerations

| Platform | I/O Backend | Special Handling |
|----------|-------------|------------------|
| Windows (MinGW) | ConPTY | Requires Windows 10 Build 17763+, uses `PeekNamedPipe` for non-blocking reads |
| macOS | forkpty | Links CoreText and CoreGraphics frameworks |
| Linux | forkpty | Links libutil for forkpty support |

## Threading Model

The PTY backends use threaded I/O to prevent blocking the GUI thread:

```mermaid
sequenceDiagram
    participant GUI as GUI Thread
    participant VIEW as wxTerminalViewCtrl
    participant PTY as PtyBackend
    participant IO as I/O Thread
    participant PROC as Child Process

    GUI->>VIEW: User types character
    VIEW->>PTY: Write(data)
    PTY->>PROC: Send to stdin
    
    PROC->>IO: Output on stdout/stderr
    IO->>PTY: on_output callback
    PTY->>VIEW: Feed(data)
    VIEW->>VIEW: Request redraw
    GUI->>VIEW: Paint event
    VIEW->>CORE: Get visible rows
    CORE-->>VIEW: Row data
    VIEW->>GUI: Render to screen
```

## Build Architecture

```mermaid
graph TB
    subgraph "Library Target"
        LIB[wxterminal_lib<br/>STATIC]
        LIB_SRC[terminal_core.cpp<br/>terminal_logger.cpp<br/>terminal_view.cpp<br/>terminal_event.cpp<br/>pty_backend_*.cpp]
    end
    
    subgraph "Demo Target"
        DEMO[wxterminal<br/>EXECUTABLE]
        DEMO_SRC[main.cpp<br/>app_persistence.cpp]
    end
    
    LIB -->|links| WX[wxWidgets]
    DEMO -->|links| LIB
    DEMO -->|links| WX
    DEMO -->|Windows| SHELL[shell32 ole32 user32]
    DEMO -->|Linux| UTIL[libutil]
    DEMO -->|macOS| FRAMEWORK[CoreText CoreGraphics]
```

## Key Architectural Decisions

1. **Static Library**: The core is built as a static library for easy integration into wxWidgets applications
2. **C++20 Standard**: Uses modern C++ features (std::optional, structured bindings, concepts where applicable)
3. **wxWidgets Integration**: Deep integration with wxWidgets event system and rendering APIs
4. **UTF-8 Throughout**: All text processing uses UTF-8 encoding
5. **Cell-Based Buffer**: Terminal content is stored as a grid of cells with individual attributes
