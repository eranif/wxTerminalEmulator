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

using terminal::ColourSpec;

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

TerminalView::TerminalView(wxWindow *parent) : wxPanel(parent, wxID_ANY) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  UpdateFontCache();

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

// Helper methods for sending special characters
void TerminalView::SendCtrlC() {
  if (m_backend) {
    m_backend->SendBreak();
  }
}

void TerminalView::SendEnter() { SendInput("\r"); }

void TerminalView::SendTab() { SendInput("\t"); }

void TerminalView::SendEscape() { SendInput("\x1b"); }

void TerminalView::SendBackspace() { SendInput("\x7f"); }

void TerminalView::SendArrowUp() { SendInput("\x1b[A"); }

void TerminalView::SendArrowDown() { SendInput("\x1b[B"); }

void TerminalView::SendArrowRight() { SendInput("\x1b[C"); }

void TerminalView::SendArrowLeft() { SendInput("\x1b[D"); }

void TerminalView::SendHome() { SendInput("\x1b[H"); }

void TerminalView::SendEnd() { SendInput("\x1b[F"); }

void TerminalView::SendDelete() { SendInput("\x1b[3~"); }

void TerminalView::SendInsert() { SendInput("\x1b[2~"); }

void TerminalView::SendPageUp() { SendInput("\x1b[5~"); }

void TerminalView::SendPageDown() { SendInput("\x1b[6~"); }

std::string TerminalView::Contents() const { return m_core.Flatten(); }

void TerminalView::SetTheme(const wxTerminalTheme &theme) {
  m_core.SetTheme(theme);
  UpdateFontCache();
  m_charH = m_charW = 0; // This needs to be recalculated based on the new font.
  m_dirty = true;
  PostSizeEvent();
  Refresh();
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

void TerminalView::UpdateFontCache() {
  m_defaultFont = m_core.GetTheme().font;
  m_defaultFontBold = m_defaultFont;
  m_defaultFontBold.MakeBold();

  m_defaultFontUnderlined = m_defaultFont;
  m_defaultFontUnderlined.MakeUnderlined();

  m_defaultFontBoldUnderlined = m_defaultFont;
  m_defaultFontBoldUnderlined.MakeBold();
  m_defaultFontBoldUnderlined.MakeUnderlined();
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

namespace {
void wxGCDCSeRenderer(wxGCDC &dc, wxPaintDC &paint_dc) {
  wxGraphicsRenderer *renderer = nullptr;
#if defined(__WXGTK__)
  renderer = wxGraphicsRenderer::GetCairoRenderer();
#elif defined(__WXMSW__) && wxUSE_GRAPHICS_DIRECT2D
  renderer = wxGraphicsRenderer::GetDirect2DRenderer();
#else
  renderer = wxGraphicsRenderer::GetDefaultRenderer();
#endif

  wxGraphicsContext *context;
  context = renderer->CreateContext(paint_dc);
  context->SetAntialiasMode(wxANTIALIAS_DEFAULT);
  dc.SetGraphicsContext(context);
}
} // namespace

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

wxColour
TerminalView::GetColourFromTheme(std::optional<terminal::ColourSpec> spec,
                                 bool foreground) const {
  const auto &theme = m_core.GetTheme();
  if (!spec.has_value() || spec->kind == ColourSpec::Kind::Default)
    return foreground ? theme.fg : theme.bg;
  switch (spec->kind) {
  case ColourSpec::Kind::Ansi:
    return theme.GetAnsiColor(spec->ansi.index, spec->ansi.bright);
  case ColourSpec::Kind::Palette256:
    return terminal::ToColour(theme.Get256Color(spec->paletteIndex));
  case ColourSpec::Kind::TrueColor:
    return terminal::ToColour(spec->rgb);
  case ColourSpec::Kind::Default:
    return foreground ? theme.fg : theme.bg;
  }
  return foreground ? theme.fg : theme.bg;
}

struct CellAttributes {
  wxColour fgColor;
  wxColour bgColor;
  bool bold;
  bool underline;
  bool isSelected;

  bool operator==(const CellAttributes &other) const {
    return fgColor == other.fgColor && bgColor == other.bgColor &&
           bold == other.bold && underline == other.underline &&
           isSelected == other.isSelected;
  }

  bool operator!=(const CellAttributes &other) const {
    return !(*this == other);
  }
};

void TerminalView::RenderRaw(wxDC &dc, int y, int rowIdx,
                             const std::vector<terminal::Cell> &row,
                             size_t &draw_text_calls) {
  const auto &theme = m_core.GetTheme();

  // Build a list of cells with their attributes, skipping truly empty ones
  struct CellInfo {
    int colIdx;
    wxChar ch;
    CellAttributes attrs;
  };
  std::vector<CellInfo> cells;
  cells.reserve(row.size());

  for (int colIdx = 0; colIdx < static_cast<int>(row.size()); ++colIdx) {
    const auto &cell = row[colIdx];

    // Check if cell is selected
    bool isApiSelected = false;
    if (m_userSelection.active) {
      std::size_t absRow = m_core.ViewStart() + rowIdx;
      isApiSelected =
          (absRow == m_userSelection.row &&
           static_cast<std::size_t>(colIdx) >= m_userSelection.col &&
           static_cast<std::size_t>(colIdx) < m_userSelection.endCol);
    }

    wxPoint current_pos(colIdx, rowIdx);
    bool isMouseSelected = m_selection.rect.Contains(current_pos);

    // Skip empty cells unless they are selected
    if (cell.IsEmpty() && !isApiSelected && !isMouseSelected) {
      continue;
    }

    // Determine colors
    wxColour bgColor = GetColourFromTheme(
        cell.colours ? cell.colours->bg : std::nullopt, false);
    wxColour fgColor = GetColourFromTheme(
        cell.colours ? cell.colours->fg : std::nullopt, true);

    if (cell.reverse) {
      std::swap(bgColor, fgColor);
    }

    bool isSelected = isApiSelected || isMouseSelected;

    CellInfo info;
    info.colIdx = colIdx;
    info.ch = static_cast<wxChar>(cell.ch);
    info.attrs.fgColor = fgColor;
    info.attrs.bgColor = isSelected ? theme.highlightBg : bgColor;
    info.attrs.bold = cell.bold;
    info.attrs.underline = cell.underline;
    info.attrs.isSelected = isSelected;

    cells.push_back(info);
  }

  // Now group and render consecutive cells with the same attributes
  if (cells.empty()) {
    return;
  }

  for (size_t i = 0; i < cells.size();) {
    const auto &firstCell = cells[i];
    wxString text;
    text.Append(firstCell.ch);

    size_t j = i + 1;
    // Group consecutive cells with same attributes
    while (j < cells.size() && cells[j].attrs == firstCell.attrs &&
           cells[j].colIdx == cells[j - 1].colIdx + 1) {
      text.Append(cells[j].ch);
      ++j;
    }

    // Draw background for the entire group
    int x = firstCell.colIdx * m_charW;
    int width = (cells[j - 1].colIdx - firstCell.colIdx + 1) * m_charW;
    dc.SetBrush(wxBrush(firstCell.attrs.bgColor));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(x, y, width, m_charH);

    // Draw text for the entire group
    dc.SetTextForeground(firstCell.attrs.fgColor);
    dc.SetFont(GetCachedFont(firstCell.attrs.bold, firstCell.attrs.underline));
    dc.DrawText(text, x, y);
    draw_text_calls++;

    i = j;
  }
}

void TerminalView::OnPaint(wxPaintEvent &) {
  // For logging purposes
  LogFunction func_timer{"TerminalView::OnPaint"};
  auto &draw_text_calls = func_timer.AddCounter("DrawText calls");

  wxAutoBufferedPaintDC dc{this};

  const auto &theme = m_core.GetTheme();
  wxRect client_rect = GetClientRect();
  dc.SetBackground(wxBrush(theme.bg));
  dc.Clear();
  dc.SetFont(m_defaultFont);

  if (m_charH == 0 && m_charW == 0) {
    // first time
    CallAfter(&TerminalView::SetTerminalSizeFromClient);
  }

  m_charW = dc.GetTextExtent("X").GetWidth();
  m_charH = dc.GetTextExtent("X").GetHeight();

  if (m_selection.active && !m_selection.empty()) {
    dc.SetBrush(wxBrush(theme.selectionBg));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(ViewCellToPixelsRect(m_selection.rect));
  }

  int y = 0;
  auto viewArea = m_core.GetViewArea();
  for (int rowIdx = 0; rowIdx < static_cast<int>(viewArea.size()); ++rowIdx) {
    const auto &row = *viewArea[rowIdx];
    RenderRaw(dc, y, rowIdx, row, draw_text_calls);
    y += m_charH;
  }

  // Draw cursor (thin line caret) — only if shell viewport is visible
  auto cursor = m_core.Cursor();
  std::size_t viewStart = m_core.ViewStart();
  std::size_t shellStart = m_core.ShellStart();
  if (viewStart <= shellStart &&
      shellStart + cursor.y < viewStart + m_core.Rows()) {
    int screenRow = static_cast<int>(shellStart - viewStart + cursor.y);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(theme.cursorColour);
    int cx = cursor.x * m_charW;
    int cy = screenRow * m_charH;
    dc.DrawRectangle(cx, cy, m_charW, m_charH);
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
  wxString selection;
  wxRect rect = m_selection.rect;
  LOG_DEBUG() << "Copying content: " << rect << std::endl;
  auto viewArea = m_core.GetViewArea();
  for (int y = rect.GetTopLeft().y;
       y <= rect.GetBottomLeft().y && y < static_cast<int>(viewArea.size());
       ++y) {
    const auto &row = *viewArea[y];
    for (int x = rect.GetTopLeft().x;
         x <= rect.GetTopRight().x && x < static_cast<int>(row.size()); ++x) {
      selection += wxUniChar(row[x].ch);
    }
    selection += "\n"; // Add newline between rows
  }

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
  ClearMouseSelection();
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
    SendEnter();
    return;
  } else if (key == WXK_TAB) {
    SendTab();
    return;
  } else if (key == WXK_ESCAPE) {
    if (m_selection.active) {
      ClearMouseSelection();
      return;
    }
    SendEscape();
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
      LOG_DEBUG() << "Sending Ctrl-C" << std::endl;
      SendCtrlC();
      return;
    }

    // Handle Ctrl+U - Clear current line (Unix-style line kill)
    if (key == 'U' || key == 'u') {
#ifdef __WXMSW__
      SendEscape();
#else
      SendInput(std::string(1, '\x15'));
#endif
      return;
    }

#ifdef __WXMSW__
    // Windows style "Ctrl-L"
    if (key == 'L' || key == 'l') {
      SendInput("cls\r");
      return;
    }
#endif

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
    SendEscape();
    SendInput(std::string(1, static_cast<char>(key)));
    return;
  }

  // Handle special keys
  // Note: ENTER, TAB, and ESCAPE are handled in OnCharHook
  if (key == WXK_BACK) {
    SendBackspace();
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
    SendInput(std::string(1, ch));
    return;
  }

  auto it = shiftMap.find(key);
  if (it != shiftMap.end()) {
    char ch = evt.ShiftDown() ? static_cast<char>(it->second)
                              : static_cast<char>(key);
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
    key == WXK_UP ? SendArrowUp() : SendArrowDown();
    return true;
  } else if (key == WXK_RIGHT) {
    SendArrowRight();
    return true;
  } else if (key == WXK_LEFT) {
    SendArrowLeft();
    return true;
  } else if (key == WXK_HOME) {
    SendHome();
    return true;
  } else if (key == WXK_END) {
    SendEnd();
    return true;
  } else if (key == WXK_DELETE) {
    SendDelete();
    return true;
  } else if (key == WXK_INSERT) {
    SendInsert();
    return true;
  } else if (key == WXK_PAGEUP) {
    SendPageUp();
    return true;
  } else if (key == WXK_PAGEDOWN) {
    SendPageDown();
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

const wxFont &TerminalView::GetCachedFont(bool bold, bool underlined) const {
  if (bold && underlined) [[unlikely]] {
    return m_defaultFontBoldUnderlined;
  } else if (bold) [[unlikely]] {
    return m_defaultFontBold;
  } else if (underlined) [[unlikely]] {
    return m_defaultFontUnderlined;
  } else [[likely]] {
    return m_defaultFont;
  }
}
