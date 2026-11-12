## Tasks

- Alternate scroll mode (DECSET 1007): on the alternate screen, when the
  program does not track the mouse, the wheel does nothing
  (`wxTerminalViewCtrl::OnMouseWheel()`). Send Up/Down cursor keys instead
  (`ESC O A` / `ESC [ A` per `IsCursorKeyMode()`), but only when the program
  enables mode 1007: on by default, tmux without `mouse on` would pass the keys
  to the shell of the pane and walk its history.
