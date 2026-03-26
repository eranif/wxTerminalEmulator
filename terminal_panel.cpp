#include "terminal_panel.h"

#include <algorithm>
#include <cstdio>
#include <unordered_map>

#include <wx/app.h>
#include <wx/clipbrd.h>
#include <wx/dcbuffer.h>
#include <wx/font.h>
#include <wx/menu.h>
#include <wx/window.h>

// Map of character key codes to their shifted values
static const std::unordered_map<int, int> shiftMap = {
    // Numbers
    {'0', ')'},
    {'1', '!'},
    {'2', '@'},
    {'3', '#'},
    {'4', '$'},
    {'5', '%'},
    {'6', '^'},
    {'7', '&'},
    {'8', '*'},
    {'9', '('},
    // Special characters
    {'\'', '"'},
    {'/', '?'},
    {';', ':'},
    {'[', '{'},
    {']', '}'},
    {'\\', '|'},
    {',', '<'},
    {'.', '>'},
    {'-', '_'},
    {'=', '+'},
    {'`', '~'}};

#ifdef __WXMAC__
constexpr int kDefaultFontSize = 18;
#else
constexpr int kDefaultFontSize = 14;
#endif

TerminalPanel::TerminalPanel(wxWindow *parent)
    : wxPanel(parent, wxID_ANY), m_timer(this) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);

  m_defaultFont =
      wxFont(wxFontInfo(kDefaultFontSize).Family(wxFONTFAMILY_TELETYPE));

  // Bind events using modern API
  Bind(wxEVT_PAINT, &TerminalPanel::OnPaint, this);
  Bind(wxEVT_SIZE, &TerminalPanel::OnSize, this);
  Bind(wxEVT_CHAR_HOOK, &TerminalPanel::OnCharHook, this);
  Bind(wxEVT_KEY_DOWN, &TerminalPanel::OnKeyDown, this);
  Bind(wxEVT_CHAR, &TerminalPanel::OnChar, this);
  Bind(wxEVT_LEFT_DOWN, &TerminalPanel::OnMouseClick, this);
  Bind(wxEVT_LEFT_UP, &TerminalPanel::OnMouseUp, this);
  Bind(wxEVT_MOTION, &TerminalPanel::OnMouseMove, this);
  Bind(wxEVT_RIGHT_DOWN, &TerminalPanel::OnRightClick, this);
  Bind(wxEVT_SET_FOCUS, &TerminalPanel::OnFocus, this);
  Bind(wxEVT_TIMER, &TerminalPanel::OnTimer, this);

  m_backend = terminal::PtyBackend::Create();

  // Set up callback for terminal responses (e.g., cursor position reports)
  m_core.SetResponseCallback(
      [this](const std::string &response) { SendInput(response); });

  SetTerminalSizeFromClient();
  m_timer.Start(16); // ~60 FPS for smooth display
  SetFocus();
}

TerminalPanel::~TerminalPanel() {
  if (m_backend)
    m_backend->Stop();
}

void TerminalPanel::Feed(const std::string &data) {
  m_core.PutData(data);
  Refresh(false);
}

void TerminalPanel::SetTerminalSizeFromClient() {
  wxClientDC dc(this);
  dc.SetFont(m_defaultFont);

  const int cw = std::max(1, dc.GetCharWidth());
  const int ch = std::max(1, dc.GetCharHeight());
  const wxSize sz = GetClientSize();

  const std::size_t cols = std::max(1, sz.GetWidth() / cw);
  const std::size_t rows = std::max(1, sz.GetHeight() / ch);

  m_core.SetViewportSize(rows, cols);
  if (m_backend)
    m_backend->Resize(static_cast<int>(cols), static_cast<int>(rows));
}

bool TerminalPanel::StartProcess(const std::string &command) {
  if (!m_backend)
    m_backend = terminal::PtyBackend::Create();
  return m_backend && m_backend->Start(command, [this](const std::string &out) {
    CallAfter(&TerminalPanel::Feed, out);
  });
}

void TerminalPanel::SendInput(const std::string &text) {
  if (m_backend) {
    m_backend->Write(text);
  }
}

std::string TerminalPanel::Contents() const { return m_core.Flatten(); }

void TerminalPanel::OnPaint(wxPaintEvent &) {
  wxAutoBufferedPaintDC dc(this);
  dc.SetBackground(*wxBLACK_BRUSH);
  dc.Clear();
  dc.SetFont(m_defaultFont);

  const int charW = dc.GetCharWidth();
  const int charH = dc.GetCharHeight();
  int rowIdx = 0;
  int y = 0;

  // Draw all cells
  for (const auto &row : m_core.Screen()) {
    int x = 0;
    int colIdx = 0;
    for (const auto &cell : row) {
      // Set background color
      wxColour bgColor((cell.bg >> 16) & 0xFF, (cell.bg >> 8) & 0xFF,
                       cell.bg & 0xFF);

      // Set foreground color
      wxColour fgColor((cell.fg >> 16) & 0xFF, (cell.fg >> 8) & 0xFF,
                       cell.fg & 0xFF);

      // Handle reverse video
      if (cell.reverse) {
        std::swap(bgColor, fgColor);
      }

      // Check if this cell is selected
      bool isSelected = false;
      if (m_selection.active) {
        int minRow = std::min(m_selection.startRow, m_selection.endRow);
        int maxRow = std::max(m_selection.startRow, m_selection.endRow);
        int minCol = std::min(m_selection.startCol, m_selection.endCol);
        int maxCol = std::max(m_selection.startCol, m_selection.endCol);

        isSelected = (rowIdx >= minRow && rowIdx <= maxRow &&
                      colIdx >= minCol && colIdx <= maxCol);
      }

      // Draw background
      dc.SetBrush(wxBrush(bgColor));
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.DrawRectangle(x, y, charW, charH);

      // Draw selection highlight (on top of background, before text)
      if (isSelected) {
        dc.SetBrush(wxBrush(
            wxColour(70, 130, 180, 100))); // Semi-transparent steel blue
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(x, y, charW, charH);
      }

      // Draw character
      dc.SetTextForeground(fgColor);

      // Set font style (bold, underline)
      wxFont font = dc.GetFont();
      if (cell.bold) {
        font.MakeBold();
      }
      if (cell.underline) {
        font.MakeUnderlined();
      }
      dc.SetFont(font);

      wxString ch;
      ch.Append(static_cast<wxChar>(cell.ch));
      dc.DrawText(ch, x, y);

      // Reset font if modified
      if (cell.bold || cell.underline) {
        dc.SetFont(m_defaultFont);
      }

      x += charW;
      colIdx++;
    }
    y += charH;
    rowIdx++;
  }

  // Draw cursor
  auto cursor = m_core.Cursor();
  if (m_cursorVisible && cursor.row < m_core.Rows() &&
      cursor.col < m_core.Cols()) {
    // Draw a solid block cursor
    dc.SetBrush(
        wxBrush(wxColour(255, 255, 255, 128))); // Semi-transparent white
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(cursor.col * charW, cursor.row * charH, charW, charH);
  }
}

void TerminalPanel::OnSize(wxSizeEvent &evt) {
  SetTerminalSizeFromClient();
  evt.Skip();
}

void TerminalPanel::OnMouseClick(wxMouseEvent &evt) {
  SetFocus();

  // Clear any existing selection when starting a new one
  m_selection.active = false;

  // Calculate which cell was clicked
  wxClientDC dc(this);
  dc.SetFont(m_defaultFont);

  const int charW = std::max(1, dc.GetCharWidth());
  const int charH = std::max(1, dc.GetCharHeight());

  int col = evt.GetX() / charW;
  int row = evt.GetY() / charH;

  // Clamp to valid range
  col = std::max(0, std::min(col, static_cast<int>(m_core.Cols()) - 1));
  row = std::max(0, std::min(row, static_cast<int>(m_core.Rows()) - 1));

  m_selection.startRow = row;
  m_selection.startCol = col;
  m_selection.endRow = row;
  m_selection.endCol = col;
  m_isDragging = true;

  Refresh();
  evt.Skip();
}

void TerminalPanel::OnMouseMove(wxMouseEvent &evt) {
  if (!m_isDragging) {
    evt.Skip();
    return;
  }

  // Calculate which cell the mouse is over
  wxClientDC dc(this);
  dc.SetFont(m_defaultFont);

  const int charW = std::max(1, dc.GetCharWidth());
  const int charH = std::max(1, dc.GetCharHeight());

  int col = evt.GetX() / charW;
  int row = evt.GetY() / charH;

  // Clamp to valid range
  col = std::max(0, std::min(col, static_cast<int>(m_core.Cols()) - 1));
  row = std::max(0, std::min(row, static_cast<int>(m_core.Rows()) - 1));

  m_selection.endRow = row;
  m_selection.endCol = col;
  m_selection.active = true;

  Refresh();
  evt.Skip();
}

void TerminalPanel::OnMouseUp(wxMouseEvent &evt) {
  m_isDragging = false;

  // If we have a selection, it's now complete
  if (m_selection.startRow != m_selection.endRow ||
      m_selection.startCol != m_selection.endCol) {
    m_selection.active = true;
  } else {
    // Just a click, no drag - clear selection
    m_selection.active = false;
  }

  Refresh();
  evt.Skip();
}

void TerminalPanel::OnRightClick(wxMouseEvent &evt) {
  wxMenu menu;

  if (m_selection.active) {
    menu.Append(wxID_COPY, "Copy");
  }
  menu.Append(wxID_PASTE, "Paste");

  menu.Bind(wxEVT_MENU, &TerminalPanel::OnCopy, this, wxID_COPY);
  menu.Bind(wxEVT_MENU, &TerminalPanel::OnPaste, this, wxID_PASTE);

  PopupMenu(&menu);
  evt.Skip();
}

void TerminalPanel::OnCopy(wxCommandEvent &evt) {
  if (!m_selection.active)
    return;

  // Get the selected text
  std::string selectedText;

  int minRow = std::min(m_selection.startRow, m_selection.endRow);
  int maxRow = std::max(m_selection.startRow, m_selection.endRow);
  int minCol = std::min(m_selection.startCol, m_selection.endCol);
  int maxCol = std::max(m_selection.startCol, m_selection.endCol);

  const auto &screen = m_core.Screen();
  for (int r = minRow; r <= maxRow && r < static_cast<int>(screen.size());
       ++r) {
    for (int c = minCol; c <= maxCol && c < static_cast<int>(screen[r].size());
         ++c) {
      selectedText += static_cast<char>(screen[r][c].ch);
    }
    if (r < maxRow) {
      selectedText += "\r\n"; // Add newline between rows
    }
  }

  // Copy to clipboard
  if (wxTheClipboard->Open()) {
    wxTheClipboard->SetData(new wxTextDataObject(selectedText));
    wxTheClipboard->Close();
  }
}

void TerminalPanel::OnPaste(wxCommandEvent &evt) {
  if (!wxTheClipboard->Open())
    return;

  if (wxTheClipboard->IsSupported(wxDF_TEXT)) {
    wxTextDataObject data;
    wxTheClipboard->GetData(data);
    std::string text = data.GetText().ToStdString();

    // Send the pasted text to the terminal
    m_currentCommand += text;
    SendInput(text);
  }

  wxTheClipboard->Close();
  evt.Skip();
}

void TerminalPanel::OnFocus(wxFocusEvent &evt) {
  // Ensure we can receive keyboard events
  evt.Skip();
}

void TerminalPanel::OnCharHook(wxKeyEvent &evt) {
  // This event is sent before the key is processed by the default handlers
  // We intercept navigation keys (Enter, Tab, Escape) here
  int key = evt.GetKeyCode();

  if (key == WXK_RETURN || key == WXK_NUMPAD_ENTER) {
    // Save current command to history (if not empty)
    if (!m_currentCommand.empty()) {
      // Don't add duplicate consecutive commands
      if (m_commandHistory.empty() ||
          m_commandHistory.back() != m_currentCommand) {
        m_commandHistory.push_back(m_currentCommand);
      }
      m_currentCommand.clear();
    }
    m_historyIndex = -1; // Reset history navigation
    SendInput("\r");
    return; // Don't skip - we handled it
  } else if (key == WXK_TAB) {
    SendInput("\t");
    m_currentCommand += '\t';
    return;
  } else if (key == WXK_ESCAPE) {
    SendInput("\x1b");
    m_currentCommand += '\x1b';
    return;
  }

  // Handle Up/Down arrows for command history
  if (key == WXK_UP) {
    if (!m_commandHistory.empty()) {
      // If we're at the current command (historyIndex == -1), save it
      if (m_historyIndex == -1) {
        // Save whatever is currently typed (in case user wants to get back to
        // it) This is stored but not added to permanent history yet
      }

      // Move back in history
      if (m_historyIndex == -1) {
        m_historyIndex = m_commandHistory.size() - 1;
      } else if (m_historyIndex > 0) {
        m_historyIndex--;
      }

      // Clear current line and replace with history item
      std::string clearAndReplace =
          std::string(m_currentCommand.length(), '\b') +
          std::string(m_currentCommand.length(), ' ') +
          std::string(m_currentCommand.length(), '\b');
      SendInput(clearAndReplace);
      SendInput(m_commandHistory[m_historyIndex]);
      m_currentCommand = m_commandHistory[m_historyIndex];
    }
    return;
  } else if (key == WXK_DOWN) {
    if (m_historyIndex >= 0) {
      // Move forward in history
      m_historyIndex++;

      // Clear current line
      std::string clearLine = std::string(m_currentCommand.length(), '\b') +
                              std::string(m_currentCommand.length(), ' ') +
                              std::string(m_currentCommand.length(), '\b');
      SendInput(clearLine);

      if (m_historyIndex < static_cast<int>(m_commandHistory.size())) {
        // Show history item
        SendInput(m_commandHistory[m_historyIndex]);
        m_currentCommand = m_commandHistory[m_historyIndex];
      } else {
        // We've gone past the end - back to empty current command
        m_historyIndex = -1;
        m_currentCommand.clear();
      }
    }
    return;
  } else if (key == WXK_ESCAPE) {
    SendInput("\x1b");
    m_currentCommand += '\x1b';
    return;
  }

  // For all other keys, let the normal event handling continue
  evt.Skip();
}

void TerminalPanel::OnKeyDown(wxKeyEvent &evt) {
  const int key = evt.GetKeyCode();

  // Handle Ctrl combinations
  const bool ctrl = evt.RawControlDown();

#ifdef __APPLE__
  // On macOS, Cmd+C/V for copy/paste (ControlDown() = Cmd key)
  if (evt.ControlDown() && !evt.AltDown()) {
    if (key == 'C' || key == 'c') {
      if (m_selection.active) {
        wxCommandEvent copyEvt(wxEVT_MENU, wxID_COPY);
        OnCopy(copyEvt);
        return;
      }
    }
    if (key == 'V' || key == 'v') {
      wxCommandEvent pasteEvt(wxEVT_MENU, wxID_PASTE);
      OnPaste(pasteEvt);
      return;
    }
  }
#endif

  if (ctrl && !evt.AltDown()) {
    // Handle Ctrl+C - Copy if text is selected, otherwise send SIGINT
    if (key == 'C' || key == 'c') {
      if (m_selection.active) {
        wxCommandEvent copyEvt(wxEVT_MENU, wxID_COPY);
        OnCopy(copyEvt);
        return;
      }
      // No selection - send Ctrl+C to terminal (SIGINT)
      m_currentCommand.clear(); // Clear current command
      m_historyIndex = -1;
    }

    // Handle Ctrl+V - Paste
    if (key == 'V' || key == 'v') {
      wxCommandEvent pasteEvt(wxEVT_MENU, wxID_PASTE);
      OnPaste(pasteEvt);
      return;
    }

    // Handle Ctrl+U - Clear current line (Unix-style line kill)
    if (key == 'U' || key == 'u') {
#ifdef _WIN32
      // On Windows, manually clear the line since cmd.exe doesn't handle ^U
      if (!m_currentCommand.empty()) {
        std::string clearLine = std::string(m_currentCommand.length(), '\b') +
                                std::string(m_currentCommand.length(), ' ') +
                                std::string(m_currentCommand.length(), '\b');
        SendInput(clearLine);
      }
#else
      // On POSIX, send the control character and let the shell handle it
      SendInput(std::string(1, '\x15'));
#endif
      m_currentCommand.clear();
      m_historyIndex = -1;
      return;
    }

    if (key >= 'A' && key <= 'Z') {
      // Ctrl+A through Ctrl+Z
      char ch = key - 'A' + 1;
      SendInput(std::string(1, ch));
      return;
    } else if (key >= 'a' && key <= 'z') {
      // Ctrl+a through Ctrl+z
      char ch = key - 'a' + 1;
      SendInput(std::string(1, ch));
      return;
    }
  }

  // Handle Alt combinations (send ESC prefix)
  if (evt.AltDown() && !ctrl && key >= 32 && key < 127) {
    SendInput("\x1b");
    SendInput(std::string(1, static_cast<char>(key)));
    return;
  }

  // Handle special keys
  // Note: ENTER, TAB, and ESCAPE are handled in OnCharHook
  if (key == WXK_BACK) {
    // Handle backspace - remove last character from current command
    if (!m_currentCommand.empty()) {
      m_currentCommand.pop_back();
    }
    m_historyIndex = -1; // Any edit resets history navigation
    SendInput("\x7f");   // Send DEL (0x7F)
    return;
  } else if (key == WXK_UP || key == WXK_DOWN) {
    // UP and DOWN are now handled in OnCharHook for command history
    // Don't pass them through to the terminal
    return;
  } else if (key == WXK_RIGHT) {
    SendInput("\x1b[C");
    return;
  } else if (key == WXK_LEFT) {
    SendInput("\x1b[D");
    return;
  } else if (key == WXK_HOME) {
    SendInput("\x1b[H");
    return;
  } else if (key == WXK_END) {
    SendInput("\x1b[F");
    return;
  } else if (key == WXK_DELETE) {
    SendInput("\x1b[3~");
    return;
  } else if (key == WXK_INSERT) {
    SendInput("\x1b[2~");
    return;
  } else if (key == WXK_PAGEUP) {
    SendInput("\x1b[5~");
    return;
  } else if (key == WXK_PAGEDOWN) {
    SendInput("\x1b[6~");
    return;
  } else if (key >= WXK_F1 && key <= WXK_F12) {
    // Function keys F1-F12
    char buf[16];
    int fkey = key - WXK_F1 + 1;
    if (fkey <= 4) {
      snprintf(buf, sizeof(buf), "\x1bO%c", 'P' + fkey - 1);
    } else {
      snprintf(buf, sizeof(buf), "\x1b[%d~", fkey + 10);
    }
    SendInput(buf);
    return;
  }

  // Handle regular printable characters
  // Note: GetUnicodeKey() doesn't work properly in KEY_DOWN on Windows
  // We need to handle case conversion ourselves
  if (key >= 'A' && key <= 'Z') {
    // Letter key - convert to lowercase unless Shift is pressed
    char ch = key;
    if (!evt.ShiftDown()) {
      ch = ch - 'A' + 'a'; // Convert to lowercase
    }
    m_currentCommand += ch;
    m_historyIndex = -1; // Any edit resets history navigation
    SendInput(std::string(1, ch));
    return;
  }

  // Handle characters that have shift variants (numbers and special characters)
  auto it = shiftMap.find(key);
  if (it != shiftMap.end()) {
    if (!evt.ShiftDown()) {
      // No shift - send the key as-is
      m_currentCommand += static_cast<char>(key);
      SendInput(std::string(1, static_cast<char>(key)));
    } else {
      // Shift pressed - send the shifted character
      m_currentCommand += static_cast<char>(it->second);
      SendInput(std::string(1, static_cast<char>(it->second)));
    }
    m_historyIndex = -1; // Any edit resets history navigation
    return;
  }

  // Handle other printable characters
  if (key >= 32 && key < 127) {
    // For other ASCII characters, send as-is
    SendInput(std::string(1, static_cast<char>(key)));
    m_currentCommand += static_cast<char>(key);
    m_historyIndex = -1; // Any edit resets history navigation
    return;
  }

  // Unknown key - skip it
  evt.Skip();
}

void TerminalPanel::OnChar(wxKeyEvent &evt) {
  // All character input is now handled in OnKeyDown
  // This prevents double-sending characters
  evt.Skip();
}

void TerminalPanel::OnTimer(wxTimerEvent &) {
  // Blink cursor every ~500ms (every 30th frame at ~16ms intervals)
  static int frameCount = 0;
  if (++frameCount % 30 == 0) {
    m_cursorVisible = !m_cursorVisible;
  }
  Refresh(false);
}
