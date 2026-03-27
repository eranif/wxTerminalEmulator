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
#include <wx/gdicmn.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/window.h>

#ifdef __WXMSW__
// We need GCDC on Windows to properly display Unicode characters.
constexpr bool kAlwaysGCDC = true;
#else
constexpr bool kAlwaysGCDC = false;
#endif

inline wxRect MakeRect(const wxPoint &p1, const wxPoint &p2) {
  const int x = std::min(p1.x, p2.x);
  const int y = std::min(p1.y, p2.y);
  const int w = std::abs(p2.x - p1.x);
  const int h = std::abs(p2.y - p1.y);
  return wxRect{x, y, w, h};
}

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
#elif defined(__WXMSW__)
      wxFont(kDefaultFontSize, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL,
             wxFONTWEIGHT_NORMAL, false, "Consolas");
#else
      wxFont(wxFontInfo(kDefaultFontSize).Family(wxFONTFAMILY_TELETYPE));
#endif

  // Bind events using modern API
  Bind(wxEVT_PAINT, &TerminalView::OnPaint, this);
  Bind(wxEVT_SIZE, &TerminalView::OnSize, this);
  Bind(wxEVT_CHAR_HOOK, &TerminalView::OnCharHook, this);
  Bind(wxEVT_KEY_DOWN, &TerminalView::OnKeyDown, this);
  Bind(wxEVT_LEFT_DOWN, &TerminalView::OnMouseLeftDown, this);
  Bind(wxEVT_LEFT_UP, &TerminalView::OnMouseUp, this);
  Bind(wxEVT_MOTION, &TerminalView::OnMouseMove, this);
  Bind(wxEVT_RIGHT_DOWN, &TerminalView::OnRightClick, this);
  Bind(wxEVT_MOUSEWHEEL, &TerminalView::OnMouseWheel, this);
  Bind(wxEVT_SET_FOCUS, &TerminalView::OnFocus, this);
  Bind(wxEVT_TIMER, &TerminalView::OnTimer, this);

  m_backend = terminal::PtyBackend::Create(GetEventHandler());

  m_timer.SetOwner(this);
  m_timer.Start(16);

  // Set up callback for terminal responses (e.g., cursor position reports)
  m_core.SetResponseCallback(
      [this](const std::string &response) { SendInput(response); });
  m_core.SetTitleCallback([this](const std::string &title) {
    wxTerminalEvent event{wxEVT_TERMINAL_TITLE_CHANGED};
    event.SetTitle(wxString::FromUTF8(title));
    event.SetEventObject(this);
    AddPendingEvent(event);
  });
  CallAfter(&TerminalView::SetFocus);
}

TerminalView::~TerminalView() {
  if (m_timer.IsRunning())
    m_timer.Stop();
  if (m_backend)
    m_backend->Stop();
}

void TerminalView::Feed(const std::string &data) {
  m_core.PutData(data);
  m_core.SetViewStart(m_core.ShellStart());
  m_dirty = true;
}

void TerminalView::SetTerminalSizeFromClient() {
  if (m_charH == 0 || m_charW == 0) {
    return;
  }
  const wxSize sz = GetClientSize();
  const std::size_t cols = std::max(1, sz.GetWidth() / m_charW);
  const std::size_t rows = std::max(1, sz.GetHeight() / m_charH);

  m_core.SetViewportSize(rows, cols);
  if (m_backend) {
    m_backend->Resize(static_cast<int>(cols), static_cast<int>(rows));
  }
}

bool TerminalView::StartProcess(const std::string &command) {
  if (!m_backend)
    m_backend = terminal::PtyBackend::Create(GetEventHandler());
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

void TerminalView::SetTheme(const wxTerminalTheme &theme) {
  m_core.SetTheme(theme);
  m_dirty = true;
  PostSizeEvent();
}

const wxTerminalTheme &TerminalView::GetTheme() const {
  return m_core.GetTheme();
}

void TerminalView::ScrollToLastLine() {
  m_core.SetViewStart(m_core.ShellStart());
  Refresh();
}

std::size_t TerminalView::GetLineCount() const { return m_core.TotalLines(); }

void TerminalView::SetBufferSize(std::size_t maxLines) {
  m_core.SetMaxLines(maxLines);
}

std::size_t TerminalView::GetBufferSize() const { return m_core.MaxLines(); }

void TerminalView::CenterLine(std::size_t line) {
  std::size_t half = m_core.Rows() / 2;
  std::size_t vs = (line > half) ? line - half : 0;
  m_core.SetViewStart(vs);
  Refresh();
}

wxString TerminalView::GetLine(std::size_t line) const {
  const auto &row = m_core.BufferRow(line);
  wxString result;
  result.reserve(row.size());
  for (const auto &cell : row)
    result += static_cast<wxChar>(cell.ch);
  result.Trim();
  return result;
}

void TerminalView::SetUserSelection(std::size_t col, std::size_t row,
                                    std::size_t count) {
  if (row >= m_core.TotalLines() || col >= m_core.Cols() || count == 0) {
    ClearUserSelection();
    return;
  }
  std::size_t endCol = std::min(col + count, m_core.Cols());
  m_userSelection = {row, col, endCol, true};
  Refresh();
}

void TerminalView::ClearUserSelection() {
  m_userSelection.active = false;
  Refresh();
}

void TerminalView::ClearMouseSelection() {
  m_selection.clear();
  m_isDragging = false;
  Refresh();
}

void TerminalView::DebugDumpViewArea() {
  auto viewArea = m_core.GetViewArea();
  size_t row_num{0};
  for (const auto &row : viewArea) {
    std::string line;
    line.reserve(row->size());
    for (const auto &cell : *row) {
      if (cell.IsEmpty()) {
        line.append(1, '_');
      } else {
        line.append(1, cell.ch);
      }
    }
    wxString line_utf8 = wxString::FromUTF8(line);
    LOG_DEBUG() << wxString::Format("%03d", row_num) << line_utf8 << std::endl;
    row_num++;
  }
}

std::unique_ptr<PaintDC> TerminalView::MakePaintDC() {
  return std::make_unique<PaintDC>(this, kAlwaysGCDC);
}

std::unique_ptr<ClientDC> TerminalView::MakeClientDC() {
  return std::make_unique<ClientDC>(this, kAlwaysGCDC);
}

wxRect TerminalView::ViewCellToPixelsRect(const wxRect &viewrect) const {
  if (m_charH == 0 || m_charW == 0) {
    return {};
  }
  if (viewrect.IsEmpty()) {
    return {};
  }

  int rect_width = viewrect.GetWidth() * m_charW;
  int rect_height = viewrect.GetHeight() * m_charH;
  int rect_x = viewrect.GetX() * m_charW;
  int rect_y = viewrect.GetY() * m_charH;

  return wxRect(rect_x, rect_y, rect_width, rect_height);
}

void TerminalView::OnPaint(wxPaintEvent &) {
  auto paint_dc = MakePaintDC();
  wxDC *dc = &paint_dc->GetDC();

  const auto &theme = m_core.GetTheme();
  wxRect client_rect = GetClientRect();
  dc->SetBackground(wxBrush(theme.bg));
  dc->Clear();
  dc->SetFont(m_defaultFont);

  if (m_charH == 0 && m_charW == 0) {
    // first time
    CallAfter(&TerminalView::SetTerminalSizeFromClient);
  }

  m_charW = dc->GetCharWidth();
  m_charH = dc->GetCharHeight();

  if (m_selection.active && !m_selection.empty()) {
    dc->SetBrush(wxBrush(theme.selectionBg));
    dc->SetPen(*wxTRANSPARENT_PEN);
    dc->DrawRectangle(ViewCellToPixelsRect(m_selection.rect));
  }

  int rowIdx = 0;
  int y = 0;
  auto viewArea = m_core.GetViewArea();
  for (std::size_t r = 0; r < viewArea.size(); ++r) {
    const auto &row = *viewArea[r];
    int x = 0;
    int colIdx = 0;
    wxString row_string;
    for (const auto &cell : row) {
      // Check selections before skipping empty cells
      bool isMouseSelected = false;
      wxPoint current_pos(colIdx, rowIdx);
      bool isApiSelected = false;
      if (m_userSelection.active) {
        std::size_t absRow = m_core.ViewStart() + r;
        isApiSelected =
            (absRow == m_userSelection.row &&
             static_cast<std::size_t>(colIdx) >= m_userSelection.col &&
             static_cast<std::size_t>(colIdx) < m_userSelection.endCol);
      }

      // Skip empty cells unless they are selected
      if (cell.IsEmpty() && !isApiSelected) {
        x += m_charW;
        colIdx++;
        continue;
      }

      wxColour bgColor = cell.GetBgColour().value_or(theme.bg);
      wxColour fgColor = cell.GetFgColour().value_or(theme.fg);
      if (cell.reverse) {
        std::swap(bgColor, fgColor);
      }

      if (!cell.IsEmpty() && !m_selection.rect.Contains(current_pos) &&
          !isApiSelected) {
        dc->SetBrush(wxBrush(bgColor));
        dc->SetPen(*wxTRANSPARENT_PEN);
        dc->DrawRectangle(x, y, m_charW, m_charH);
      }

      if (isApiSelected) {
        dc->SetBrush(wxBrush(theme.highlightBg));
        dc->SetPen(*wxTRANSPARENT_PEN);
        dc->DrawRectangle(x, y, m_charW, m_charH);
      }

      dc->SetTextForeground(fgColor);
      wxFont font = dc->GetFont();
      if (cell.bold)
        font.MakeBold();
      if (cell.underline)
        font.MakeUnderlined();
      dc->SetFont(font);

      wxString ch{wxUniChar(cell.ch), 1};
      row_string << ch;
      dc->DrawText(ch, x, y);

      if (cell.bold || cell.underline)
        dc->SetFont(m_defaultFont);

      x += m_charW;
      colIdx++;
    }
    y += m_charH;
    rowIdx++;
  }

  // Draw cursor (thin line caret) — only if shell viewport is visible
  auto cursor = m_core.Cursor();
  std::size_t viewStart = m_core.ViewStart();
  std::size_t shellStart = m_core.ShellStart();
  if (viewStart <= shellStart &&
      shellStart + cursor.y < viewStart + m_core.Rows()) {
    int screenRow = static_cast<int>(shellStart - viewStart + cursor.y);
    dc->SetPen(wxPen(theme.cursorColour, 2));
    int cx = cursor.x * m_charW;
    int cy = screenRow * m_charH;
    dc->DrawLine(cx, cy, cx, cy + m_charH);
  }
}

void TerminalView::OnSize(wxSizeEvent &evt) {
  SetTerminalSizeFromClient();
  evt.Skip();
}

void TerminalView::OnMouseLeftDown(wxMouseEvent &evt) {
  evt.Skip();
  if (m_charW == 0 || m_charH == 0) {
    return;
  }

  // If a selection already exists, a click should clear it and stop here
  // rather than starting a new 1-cell selection.
  if (m_selection.active) {
    m_selection.clear();
    m_isDragging = false;
    return;
  }

  int x = evt.GetX() / m_charW;
  int y = evt.GetY() / m_charH;

  // Clamp to valid range
  x = std::max(0, std::min(x, static_cast<int>(m_core.Cols()) - 1));
  y = std::max(0, std::min(y, static_cast<int>(m_core.Rows()) - 1));

  // Start with a 1x1 rectangle so a simple click selects the line/cell
  // under the mouse. A zero-sized wxRect is considered empty and will not
  // paint or copy anything.
  m_selection.rect = wxRect(wxPoint(x, y), wxSize(1, 1));
  m_selection.active = true;
  m_isDragging = true;
}

void TerminalView::OnMouseMove(wxMouseEvent &evt) {
  evt.Skip();
  if (!m_isDragging || !m_selection.active) {
    return;
  }

  if (m_charW == 0 || m_charH == 0) {
    return;
  }

  int x = evt.GetX() / m_charW;
  int y = evt.GetY() / m_charH;

  // Clamp to valid range
  x = std::max(0, std::min(x, static_cast<int>(m_core.Cols()) - 1));
  y = std::max(0, std::min(y, static_cast<int>(m_core.Rows()) - 1));

  wxPoint pt1 = m_selection.rect.GetPosition();
  wxPoint pt2{x, y};
  m_selection.rect = MakeRect(pt1, pt2);
  if (m_selection.rect.width == 0) {
    m_selection.rect.width = 1;
  }
  if (m_selection.rect.height == 0) {
    m_selection.rect.height = 1;
  }
  m_selection.active = true;
  Refresh();
}

void TerminalView::OnMouseUp(wxMouseEvent &evt) {
  m_isDragging = false;

  // If we have a selection, it's now complete
  if (!m_selection.empty()) {
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

  m_selection.active = false; // Cancel any selection when scrolling or the
                              // selected area will scroll with us
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
  if (!m_selection.active) {
    LOG_DEBUG() << "No selection is active - will do nothing" << std::endl;
    return;
  }

  // Get the selected text
  std::string selectedText;
  wxRect rect = m_selection.rect;
  LOG_DEBUG() << "Copying content: " << rect << std::endl;
  auto viewArea = m_core.GetViewArea();
  for (int y = rect.GetTopLeft().y;
       y <= rect.GetBottomLeft().y && y < static_cast<int>(viewArea.size());
       ++y) {
    const auto &row = *viewArea[y];
    for (int x = rect.GetTopLeft().x;
         x <= rect.GetTopRight().x && x < static_cast<int>(row.size()); ++x) {
      selectedText += static_cast<char>(row[x].ch);
    }
    selectedText += "\n"; // Add newline between rows
  }

  wxString selection = wxString::FromUTF8(selectedText);
  if (!selection.empty()) {
    selection.RemoveLast();
  }

  LOG_DEBUG() << "Copying:" << selection.size() << " chars. Content:\n"
              << selection << std::endl;
  // Copy to clipboard
  if (wxTheClipboard->Open()) {
    wxTheClipboard->SetData(new wxTextDataObject(selection));
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
    SendInput("\r");
    return;
  } else if (key == WXK_TAB) {
    SendInput("\t");
    return;
  } else if (key == WXK_ESCAPE) {
    if (m_selection.active) {
      ClearMouseSelection();
      return;
    }
    SendInput("\x1b");
    return;
  }

#ifdef __WXMSW__
  // Special keys should be handled here on Windows.
  if (HandleSpecialKeys(evt)) {
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
    SendInput("\x7f");
    return;
  }

#ifndef __WXMSW__
  if (HandleSpecialKeys(evt)) {
    return;
  }
#endif

  // Handle regular printable characters
  // Note: GetUnicodeKey() doesn't work properly in KEY_DOWN on Windows
  // We need to handle case conversion ourselves
  if (key >= 'A' && key <= 'Z') {
    char ch = key;
    if (!evt.ShiftDown()) {
      ch = ch - 'A' + 'a';
    }
#ifdef _WIN32
#endif
    SendInput(std::string(1, ch));
    return;
  }

  auto it = shiftMap.find(key);
  if (it != shiftMap.end()) {
    char ch = evt.ShiftDown() ? static_cast<char>(it->second)
                              : static_cast<char>(key);
#ifdef _WIN32
#endif
    SendInput(std::string(1, ch));
    return;
  }

  if (key >= 32 && key < 127) {
    SendInput(std::string(1, static_cast<char>(key)));
#ifdef _WIN32
#endif
    return;
  }

  // Unknown key - skip it
  evt.Skip();
}

bool TerminalView::HandleSpecialKeys(wxKeyEvent &key_event) {
  auto key = key_event.GetKeyCode();
  if (key == WXK_UP || key == WXK_DOWN) {
    SendInput(key == WXK_UP ? "\x1b[A" : "\x1b[B");
    return true;
  } else if (key == WXK_RIGHT) {
    SendInput("\x1b[C");
    return true;
  } else if (key == WXK_LEFT) {
    SendInput("\x1b[D");
    return true;
  } else if (key == WXK_HOME) {
    SendInput("\x1b[H");
    return true;
  } else if (key == WXK_END) {
    SendInput("\x1b[F");
    return true;
  } else if (key == WXK_DELETE) {
    SendInput("\x1b[3~");
    return true;
  } else if (key == WXK_INSERT) {
    SendInput("\x1b[2~");
    return true;
  } else if (key == WXK_PAGEUP) {
    SendInput("\x1b[5~");
    return true;
  } else if (key == WXK_PAGEDOWN) {
    SendInput("\x1b[6~");
    return true;
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
    return true;
  }
  return false;
}

void TerminalView::OnTimer(wxTimerEvent &evt) {
  if (m_dirty) {
    m_dirty = false;
    Refresh(false);
  }
}
