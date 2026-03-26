#include "terminal_view.h"

#include "terminal_event.h"
#include "terminal_logger.h"
#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <wx/app.h>
#include <wx/clipbrd.h>
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/font.h>
#include <wx/frame.h>
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
constexpr int kDefaultFontSize = 20;
#else
constexpr int kDefaultFontSize = 14;
#endif

TerminalView::TerminalView(wxWindow *parent) : wxPanel(parent, wxID_ANY) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);

  m_defaultFont =
#ifdef __WXMAC__
      wxFont(kDefaultFontSize, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL,
             wxFONTWEIGHT_NORMAL, false, "Menlo");
#else
      wxFont(wxFontInfo(kDefaultFontSize).Family(wxFONTFAMILY_TELETYPE));
#endif

  // Bind events using modern API
  Bind(wxEVT_PAINT, &TerminalView::OnPaint, this);
  Bind(wxEVT_SIZE, &TerminalView::OnSize, this);
  Bind(wxEVT_CHAR_HOOK, &TerminalView::OnCharHook, this);
  Bind(wxEVT_KEY_DOWN, &TerminalView::OnKeyDown, this);
  Bind(wxEVT_LEFT_DOWN, &TerminalView::OnMouseClick, this);
  Bind(wxEVT_LEFT_UP, &TerminalView::OnMouseUp, this);
  Bind(wxEVT_MOTION, &TerminalView::OnMouseMove, this);
  Bind(wxEVT_RIGHT_DOWN, &TerminalView::OnRightClick, this);
  Bind(wxEVT_MOUSEWHEEL, &TerminalView::OnMouseWheel, this);
  Bind(wxEVT_SET_FOCUS, &TerminalView::OnFocus, this);
  Bind(wxEVT_IDLE, &TerminalView::OnIdle, this);

  m_backend = terminal::PtyBackend::Create();

  // Set up callback for terminal responses (e.g., cursor position reports)
  m_core.SetResponseCallback(
      [this](const std::string &response) { SendInput(response); });
  m_core.SetTitleCallback([this](const std::string &title) {
    wxTerminalEvent event{wxEVT_TERMINAL_TITLE_CHANGED};
    event.SetTitle(wxString::FromUTF8(title));
    event.SetEventObject(this);
    AddPendingEvent(event);
  });

  SetTerminalSizeFromClient();
  SetFocus();
}

TerminalView::~TerminalView() {
  if (m_backend)
    m_backend->Stop();
}

void TerminalView::Feed(const std::string &data) {
  m_core.PutData(data);
  m_core.SetViewStart(m_core.ShellStart());
  m_dirty = true;
}

void TerminalView::SetTerminalSizeFromClient() {
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

bool TerminalView::StartProcess(const std::string &command) {
  if (!m_backend)
    m_backend = terminal::PtyBackend::Create();
  return m_backend && m_backend->Start(command, [this](const std::string &out) {
    CallAfter(&TerminalView::Feed, out);
  });
}

void TerminalView::SendInput(const std::string &text) {
  if (m_backend) {
    m_backend->Write(text);
  }
}

std::string TerminalView::Contents() const { return m_core.Flatten(); }

void TerminalView::OnPaint(wxPaintEvent &) {
  wxAutoBufferedPaintDC pdc(this);
  wxGCDC dc(pdc);
  dc.SetBackground(*wxBLACK_BRUSH);
  dc.Clear();
  dc.SetFont(m_defaultFont);

  const int charW = dc.GetCharWidth();
  const int charH = dc.GetCharHeight();
  int rowIdx = 0;
  int y = 0;
  auto viewArea = m_core.GetViewArea();

  for (std::size_t r = 0; r < viewArea.size(); ++r) {
    const auto &row = *viewArea[r];
    int x = 0;
    int colIdx = 0;
    for (const auto &cell : row) {
      wxColour bgColor((cell.bg >> 16) & 0xFF, (cell.bg >> 8) & 0xFF,
                       cell.bg & 0xFF);
      wxColour fgColor((cell.fg >> 16) & 0xFF, (cell.fg >> 8) & 0xFF,
                       cell.fg & 0xFF);
      if (cell.reverse)
        std::swap(bgColor, fgColor);

      bool isSelected = false;
      if (m_selection.active) {
        int minRow = std::min(m_selection.startRow, m_selection.endRow);
        int maxRow = std::max(m_selection.startRow, m_selection.endRow);
        int minCol = std::min(m_selection.startCol, m_selection.endCol);
        int maxCol = std::max(m_selection.startCol, m_selection.endCol);
        isSelected = (rowIdx >= minRow && rowIdx <= maxRow &&
                      colIdx >= minCol && colIdx <= maxCol);
      }

      dc.SetBrush(wxBrush(bgColor));
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.DrawRectangle(x, y, charW, charH);

      if (isSelected) {
        dc.SetBrush(wxBrush(wxColour(70, 130, 180, 100)));
        dc.SetPen(*wxTRANSPARENT_PEN);
        dc.DrawRectangle(x, y, charW, charH);
      }

      dc.SetTextForeground(fgColor);
      wxFont font = dc.GetFont();
      if (cell.bold)
        font.MakeBold();
      if (cell.underline)
        font.MakeUnderlined();
      dc.SetFont(font);

      wxString ch;
      ch.Append(static_cast<wxChar>(cell.ch));
      dc.DrawText(ch, x, y);

      if (cell.bold || cell.underline)
        dc.SetFont(m_defaultFont);

      x += charW;
      colIdx++;
    }
    y += charH;
    rowIdx++;
  }

  // Draw cursor (thin line caret) — only if shell viewport is visible
  auto cursor = m_core.Cursor();
  std::size_t viewStart = m_core.ViewStart();
  std::size_t shellStart = m_core.ShellStart();
  if (viewStart <= shellStart &&
      shellStart + cursor.row < viewStart + m_core.Rows()) {
    int screenRow = static_cast<int>(shellStart - viewStart + cursor.row);
    dc.SetPen(wxPen(*wxWHITE, 2));
    int cx = cursor.col * charW;
    int cy = screenRow * charH;
    dc.DrawLine(cx, cy, cx, cy + charH);
  }
}

void TerminalView::OnSize(wxSizeEvent &evt) {
  SetTerminalSizeFromClient();
  evt.Skip();
}

void TerminalView::OnMouseClick(wxMouseEvent &evt) {
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

void TerminalView::OnMouseMove(wxMouseEvent &evt) {
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

void TerminalView::OnMouseUp(wxMouseEvent &evt) {
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

void TerminalView::OnMouseWheel(wxMouseEvent &evt) {
  m_wheelAccum += evt.GetWheelRotation();
  int delta = evt.GetWheelDelta();
  int lines = m_wheelAccum / delta;
  m_wheelAccum %= delta;

  if (lines == 0)
    return;

  std::size_t vs = m_core.ViewStart();
  if (lines > 0 && vs >= static_cast<std::size_t>(lines))
    m_core.SetViewStart(vs - lines);
  else if (lines > 0)
    m_core.SetViewStart(0);
  else
    m_core.SetViewStart(vs + static_cast<std::size_t>(-lines));

  m_dirty = true;
}

void TerminalView::OnRightClick(wxMouseEvent &evt) {
  wxMenu menu;

  if (m_selection.active) {
    menu.Append(wxID_COPY, "Copy");
  }
  menu.Append(wxID_PASTE, "Paste");

  menu.Bind(wxEVT_MENU, &TerminalView::OnCopy, this, wxID_COPY);
  menu.Bind(wxEVT_MENU, &TerminalView::OnPaste, this, wxID_PASTE);

  PopupMenu(&menu);
  evt.Skip();
}

void TerminalView::OnCopy(wxCommandEvent &evt) {
  LOG_DEBUG() << "Copy is called!" << std::endl;
  LOG_DEBUG() << "Copy is called!" << std::endl;
  if (!m_selection.active) {
    LOG_DEBUG() << "No selection is active - will do nothing" << std::endl;
    return;
  }

  // Get the selected text
  wxString selectedText;

  int minRow = std::min(m_selection.startRow, m_selection.endRow);
  int maxRow = std::max(m_selection.startRow, m_selection.endRow);
  int minCol = std::min(m_selection.startCol, m_selection.endCol);
  int maxCol = std::max(m_selection.startCol, m_selection.endCol);
  LOG_DEBUG() << "Copying content {" << minCol << "," << minRow << "} => {"
              << maxCol << "," << maxRow << "}" << std::endl;
  auto viewArea = m_core.GetViewArea();
  for (int r = minRow; r <= maxRow && r < static_cast<int>(viewArea.size());
       ++r) {
    const auto &row = *viewArea[r];
    for (int c = minCol; c <= maxCol && c < static_cast<int>(row.size()); ++c) {
      selectedText += static_cast<wxChar>(row[c].ch);
    }
    if (r < maxRow) {
      selectedText += "\n"; // Add newline between rows
    }
  }

  LOG_DEBUG() << "Copying:" << selectedText.size() << " chars. " << selectedText
              << std::endl;
  // Copy to clipboard
  if (wxTheClipboard->Open()) {
    wxTheClipboard->SetData(new wxTextDataObject(selectedText));
    wxTheClipboard->Flush();
    wxTheClipboard->Close();
  }
  m_selection.active = false;
  Refresh();
}

void TerminalView::OnPaste(wxCommandEvent &evt) {
  LOG_DEBUG() << "Paste is called!" << std::endl;
  if (!wxTheClipboard->Open())
    return;

  if (wxTheClipboard->IsSupported(wxDF_UNICODETEXT) ||
      wxTheClipboard->IsSupported(wxDF_TEXT)) {
    wxTextDataObject data;
    wxTheClipboard->GetData(data);
    std::string text = data.GetText().ToStdString(wxConvUTF8);
#ifdef _WIN32
    m_currentCommand += text;
#endif
    SendInput(text);
  }

  wxTheClipboard->Close();
  evt.Skip();
}

void TerminalView::OnFocus(wxFocusEvent &evt) {
  // Ensure we can receive keyboard events
  evt.Skip();
}

void TerminalView::OnCharHook(wxKeyEvent &evt) {
  // This event is sent before the key is processed by the default handlers
  // We intercept navigation keys (Enter, Tab, Escape) here
  int key = evt.GetKeyCode();

  if (key == WXK_RETURN || key == WXK_NUMPAD_ENTER) {
    LOG_TRACE() << "Handling WXK_RETURN" << std::endl;
#ifdef _WIN32
    if (!m_currentCommand.empty()) {
      if (m_commandHistory.empty() ||
          m_commandHistory.back() != m_currentCommand) {
        m_commandHistory.push_back(m_currentCommand);
      }
      m_currentCommand.clear();
    }
    m_historyIndex = -1;
#endif
    SendInput("\r");
    LOG_TRACE() << "SendInput is called with \\r" << std::endl;
    return;
  } else if (key == WXK_TAB) {
    SendInput("\t");
    return;
  } else if (key == WXK_ESCAPE) {
    SendInput("\x1b");
    return;
  }

#ifdef _WIN32
  // Application-level command history (Windows only — POSIX shells have
  // readline)
  if (key == WXK_UP) {
    if (!m_commandHistory.empty()) {
      if (m_historyIndex == -1) {
        m_historyIndex = m_commandHistory.size() - 1;
      } else if (m_historyIndex > 0) {
        m_historyIndex--;
      }
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
      m_historyIndex++;
      std::string clearLine = std::string(m_currentCommand.length(), '\b') +
                              std::string(m_currentCommand.length(), ' ') +
                              std::string(m_currentCommand.length(), '\b');
      SendInput(clearLine);
      if (m_historyIndex < static_cast<int>(m_commandHistory.size())) {
        SendInput(m_commandHistory[m_historyIndex]);
        m_currentCommand = m_commandHistory[m_historyIndex];
      } else {
        m_historyIndex = -1;
        m_currentCommand.clear();
      }
    }
    return;
  }
#endif

  // For all other keys, let the normal event handling continue
  evt.Skip();
}

void TerminalView::OnKeyDown(wxKeyEvent &evt) {
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
#ifdef _WIN32
      m_currentCommand.clear();
      m_historyIndex = -1;
#endif
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
      if (!m_currentCommand.empty()) {
        std::string clearLine = std::string(m_currentCommand.length(), '\b') +
                                std::string(m_currentCommand.length(), ' ') +
                                std::string(m_currentCommand.length(), '\b');
        SendInput(clearLine);
      }
      m_currentCommand.clear();
      m_historyIndex = -1;
#else
      SendInput(std::string(1, '\x15'));
#endif
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
#ifdef _WIN32
    if (!m_currentCommand.empty()) {
      m_currentCommand.pop_back();
    }
    m_historyIndex = -1;
#endif
    SendInput("\x7f");
    return;
  } else if (key == WXK_UP || key == WXK_DOWN) {
#ifdef _WIN32
    // UP and DOWN are handled in OnCharHook for command history on Windows
    return;
#else
    // On POSIX, send arrow escape sequences — shell handles history
    SendInput(key == WXK_UP ? "\x1b[A" : "\x1b[B");
    return;
#endif
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
    char ch = key;
    if (!evt.ShiftDown()) {
      ch = ch - 'A' + 'a';
    }
#ifdef _WIN32
    m_currentCommand += ch;
    m_historyIndex = -1;
#endif
    SendInput(std::string(1, ch));
    return;
  }

  auto it = shiftMap.find(key);
  if (it != shiftMap.end()) {
    char ch = evt.ShiftDown() ? static_cast<char>(it->second)
                              : static_cast<char>(key);
#ifdef _WIN32
    m_currentCommand += ch;
    m_historyIndex = -1;
#endif
    SendInput(std::string(1, ch));
    return;
  }

  if (key >= 32 && key < 127) {
    SendInput(std::string(1, static_cast<char>(key)));
#ifdef _WIN32
    m_currentCommand += static_cast<char>(key);
    m_historyIndex = -1;
#endif
    return;
  }

  // Unknown key - skip it
  evt.Skip();
}

void TerminalView::OnIdle(wxIdleEvent &evt) {
  if (m_dirty) {
    m_dirty = false;
    Refresh(false);
  }
}
