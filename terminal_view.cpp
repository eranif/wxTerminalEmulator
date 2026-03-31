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

#ifdef __WXOSX__
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#endif

using terminal::ColourSpec;

inline wxRect MakeRect(const wxPoint &p1, const wxPoint &p2) {
  const int x = std::min(p1.x, p2.x);
  const int y = std::min(p1.y, p2.y);
  const int w = std::abs(p2.x - p1.x);
  const int h = std::abs(p2.y - p1.y);
  return wxRect{x, y, w, h};
}

void TerminalView::SelectionRect::Clear() {
  m_rect = {};
  m_selectionAnchor = {};
}

wxRect
TerminalView::SelectionRect::PixelsRectToViewCellRect(int char_width,
                                                      int char_height) const {
  if (char_width == 0 || char_height == 0 || m_rect.IsEmpty()) {
    return {};
  }

  int cell_x = m_rect.GetX() / char_width;
  int cell_y = m_rect.GetY() / char_height;
  int pixel_right = m_rect.GetRight();
  int pixel_bottom = m_rect.GetBottom();
  int cell_right = (pixel_right + char_width - 1) / char_width - 1;
  int cell_bottom = (pixel_bottom + char_height - 1) / char_height - 1;
  int cell_width = cell_right - cell_x + 1;
  int cell_height = cell_bottom - cell_y + 1;
  return wxRect(cell_x, cell_y, cell_width, cell_height);
}

void TerminalView::SelectionRect::SnapToCellGrid(int char_width,
                                                 int char_height) {
  if (char_width == 0 || char_height == 0 || m_rect.IsEmpty()) {
    return;
  }

  m_rect =
      ViewCellToPixelsRect(PixelsRectToViewCellRect(char_width, char_height),
                           char_width, char_height);
}

wxRect TerminalView::SelectionRect::ViewCellToPixelsRect(
    const wxRect &viewrect, int char_width, int char_height) const {
  if (char_width == 0 || char_height == 0 || viewrect.IsEmpty()) {
    return {};
  }

  return wxRect(viewrect.GetX() * char_width, viewrect.GetY() * char_height,
                viewrect.GetWidth() * char_width,
                viewrect.GetHeight() * char_height);
}

bool TerminalView::SelectionRect::IsSelectionRectHasMinSize() const {
  static constexpr int kMinRectSize = 2;
  return !m_rect.IsEmpty() && (m_rect.GetSize().GetWidth() >= kMinRectSize ||
                               m_rect.GetSize().GetHeight() >= kMinRectSize);
}

void TerminalView::SelectionRect::SetAnchor(const wxPoint &anchor) {
  m_selectionAnchor = anchor;
  m_rect = wxRect(anchor, wxSize{1, 1});
}

void TerminalView::SelectionRect::UpdateCurrent(const wxPoint &current) {
  m_rect = MakeRect(m_selectionAnchor, current);
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

TerminalView::TerminalView(wxWindow *parent, const wxString &shellCommand,
                           const std::optional<EnvironmentList> &environment)
    : wxPanel(parent, wxID_ANY) {
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
  Bind(wxEVT_CONTEXT_MENU, &TerminalView::OnContextMenu, this);
  Bind(wxEVT_MOUSEWHEEL, &TerminalView::OnMouseWheel, this);
  Bind(wxEVT_SET_FOCUS, &TerminalView::OnFocus, this);
  Bind(wxEVT_KILL_FOCUS, &TerminalView::OnLostFocus, this);
  Bind(wxEVT_TIMER, &TerminalView::OnTimer, this);
  Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &e) {
    e.Skip();
    if (!m_contextMenuShowing) {
      TLOG_DEBUG() << "Leaving window!" << std::endl;
      ClearMouseSelection();
    }
  });
  Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent &) {});
  m_shell_command = shellCommand;
  m_environment = std::move(environment);

  m_timer.SetOwner(this);
  m_timer.Start(16); // Roughly 60fps

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
  m_needsRepaint = true;
}

void TerminalView::SetTerminalSizeFromClient() {
  if (m_charH == 0 || m_charW == 0) {
    return;
  }
  const wxSize sz = GetClientSize();
  const std::size_t cols = std::max(1, sz.GetWidth() / m_charW);
  const std::size_t rows = std::max(1, sz.GetHeight() / m_charH);

  if (cols == m_core.Cols() && rows == m_core.Rows())
    return;

  TLOG_DEBUG() << "Resize: " << cols << "x" << rows << " (charW=" << m_charW
               << " charH=" << m_charH << ")" << std::endl;
  m_core.SetViewportSize(rows, cols);
  if (m_backend) {
    m_backend->Resize(static_cast<int>(cols), static_cast<int>(rows));
  }
}

void TerminalView::StartProcess(
    const wxString &command,
    const std::optional<EnvironmentList> &environment) {
  if (m_backend)
    return;
  m_backend = terminal::PtyBackend::Create(GetEventHandler());
  m_shell_command = command;
  m_environment = environment;

  TLOG_DEBUG() << "Starting shell with command: " << command << std::endl;
  bool ok = m_backend &&
            m_backend->Start(m_shell_command.ToStdString(wxConvUTF8),
                             m_environment, [this](const std::string &out) {
                               CallAfter(&TerminalView::Feed, out);
                             });
  if (ok && m_core.Cols() > 0 && m_core.Rows() > 0) {
    m_backend->Resize(static_cast<int>(m_core.Cols()),
                      static_cast<int>(m_core.Rows()));
  }
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

void TerminalView::SendCtrlR() { SendInput("\x12"); }

void TerminalView::SendCtrlU() {
  wxString lower_case_shell = m_shell_command.Lower();
  if (!lower_case_shell.empty() && (lower_case_shell.Contains("cmd") ||
                                    lower_case_shell.Contains("powershell"))) {
    // Windows style "Ctrl-U" for CMD / PS
    SendEscape();
    return;
  }
  SendInput("\x15");
}

void TerminalView::SendCtrlL() {
  wxString lower_case_shell = m_shell_command.Lower();
  if (!lower_case_shell.empty() && (lower_case_shell.Contains("cmd") ||
                                    lower_case_shell.Contains("powershell"))) {
    // Windows style "Ctrl-L" for CMD / PS
    SendInput("cls\r");
    return;
  }
  SendInput("\x0c");
}

void TerminalView::SendCtrlD() { SendInput("\x04"); }
void TerminalView::SendCtrlW() { SendInput("\x17"); }
void TerminalView::SendCtrlZ() { SendInput("\x1a"); }

void TerminalView::SendCtrlK() {
  wxString lower_case_shell = m_shell_command.Lower();
  if (!lower_case_shell.empty() && (lower_case_shell.Contains("cmd") ||
                                    lower_case_shell.Contains("powershell"))) {
    // Windows shells typically use a command-line kill behavior via Escape.
    SendEscape();
    return;
  }
  SendInput("\x0b");
}

void TerminalView::SendCtrlA() {
  wxString lower_case_shell = m_shell_command.Lower();
  if (!lower_case_shell.empty() && (lower_case_shell.Contains("cmd") ||
                                    lower_case_shell.Contains("powershell"))) {
    // Windows shells often interpret Ctrl-A as Select All rather than
    // beginning-of-line editing.
    SendInput("^a");
    return;
  }
  SendInput("\x01");
}

void TerminalView::SendCtrlE() {
  wxString lower_case_shell = m_shell_command.Lower();
  if (!lower_case_shell.empty() && (lower_case_shell.Contains("cmd") ||
                                    lower_case_shell.Contains("powershell"))) {
    // No special Windows equivalent; fall back to the raw control code.
    SendInput("\x05");
    return;
  }
  SendInput("\x05");
}

void TerminalView::SendAltB() {
  wxString lower_case_shell = m_shell_command.Lower();
  if (!lower_case_shell.empty() && (lower_case_shell.Contains("cmd") ||
                                    lower_case_shell.Contains("powershell"))) {
    // Use a common readline-style escape sequence only when supported.
    SendInput("\x1b"
              "b");
    return;
  }
  SendInput("\x1b"
            "b");
}

void TerminalView::SendAltF() {
  wxString lower_case_shell = m_shell_command.Lower();
  if (!lower_case_shell.empty() && (lower_case_shell.Contains("cmd") ||
                                    lower_case_shell.Contains("powershell"))) {
    // Use a common readline-style escape sequence only when supported.
    SendInput("\x1b"
              "f");
    return;
  }
  SendInput("\x1b"
            "f");
}

wxString TerminalView::GetText() const { return m_core.Flatten(); }

void TerminalView::SetTheme(const wxTerminalTheme &theme) {
  m_core.SetTheme(theme);
  UpdateFontCache();
  m_charH = m_charW = 0; // This needs to be recalculated based on the new font.
  Refresh();
  PostSizeEvent();
}

const wxTerminalTheme &TerminalView::GetTheme() const {
  return m_core.GetTheme();
}

void TerminalView::ScrollToLastLine() {
  m_core.SetViewStart(m_core.ShellStart());
  m_needsRepaint = true;
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
  m_needsRepaint = true;
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
  m_needsRepaint = true;
}

void TerminalView::ClearUserSelection() {
  m_userSelection.active = false;
  m_needsRepaint = true;
}

void TerminalView::ClearMouseSelection() {
  m_mouseSelectionRect.Clear();
  m_isDragging = false;
  m_needsRepaint = true;
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
  TLOG_IF_DEBUG {
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
      TLOG_DEBUG() << wxString::Format("%03d", row_num) << line_utf8
                   << std::endl;
      row_num++;
    }
  }
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

std::vector<TerminalView::CellInfo>
TerminalView::PrepareRowForDrawing(const std::vector<terminal::Cell> &row,
                                   int rowIdx, const wxRect &selected_cells) {
  const auto &theme = m_core.GetTheme();
  // Build a list of cells with their attributes, skipping truly empty ones
  std::vector<CellInfo> cells;
  cells.reserve(row.size());

  // Collect drawable cells
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
    bool isMouseSelected = selected_cells.Contains(current_pos);

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

    CellInfo info;
    info.colIdx = colIdx;
    info.ch = static_cast<wxChar>(cell.ch);
    info.attrs.fgColor = fgColor;
    if (isMouseSelected) {
      info.attrs.bgColor = theme.selectionBg;
    } else if (isApiSelected) {
      info.attrs.bgColor = theme.highlightBg;
    } else {
      info.attrs.bgColor = theme.bg;
    }
    info.attrs.bold = cell.bold;
    info.attrs.underline = cell.underline;
    info.attrs.isMouseSelected = isMouseSelected;
    info.attrs.isApiSelected = isApiSelected;
    cells.push_back(info);
  }
  return cells;
}

bool TerminalView::IsAsciiSafeTextRun(
    const std::vector<TerminalView::CellInfo> &cells) const {
  if (cells.empty()) {
    return false;
  }

  for (const auto &cell : cells) {
    if (!IsUnicodeSingleCellSafe(cell.ch)) {
      return false;
    }
  }

  return true;
}

bool TerminalView::IsUnicodeSingleCellSafe(wxChar ch) const {
  if (ch >= 0x20 && ch <= 0x7e) {
    return true;
  }

  if (ch < 0x80) {
    return false;
  }

  // Reject control characters and combining marks.
  if ((ch >= 0x00 && ch < 0x20) || ch == 0x7f) {
    return false;
  }

  // Reject common zero-width / combining / joiner code points.
  switch (ch) {
  case 0x0300: // Combining Grave Accent
  case 0x0301:
  case 0x0302:
  case 0x0303:
  case 0x0304:
  case 0x0308:
  case 0x030A:
  case 0x0327:
  case 0x200C: // ZWNJ
  case 0x200D: // ZWJ
  case 0xFE0E: // Variation Selector-15
  case 0xFE0F: // Variation Selector-16
    return false;
  default:
    break;
  }

  // Reject common wide/full-width blocks that usually span more than one cell.
  if ((ch >= 0x1100 && ch <= 0x115F) || (ch >= 0x2329 && ch <= 0x232A) ||
      (ch >= 0x2E80 && ch <= 0xA4CF) || (ch >= 0xAC00 && ch <= 0xD7A3) ||
      (ch >= 0xF900 && ch <= 0xFAFF) || (ch >= 0xFE10 && ch <= 0xFE19) ||
      (ch >= 0xFE30 && ch <= 0xFE6F) || (ch >= 0xFF00 && ch <= 0xFF60) ||
      (ch >= 0xFFE0 && ch <= 0xFFE6)) {
    return false;
  }

  // A conservative whitelist of single-cell Unicode blocks that are typically
  // safe to render as one terminal cell.
  if ((ch >= 0x00A0 && ch <= 0x024F) || // Latin-1 supplement + extended Latin
      (ch >= 0x0370 && ch <= 0x052F) || // Greek, Cyrillic, Armenian
      (ch >= 0x2000 && ch <= 0x206F) || // General punctuation
      (ch >= 0x2100 && ch <= 0x214F) || // Letterlike symbols
      (ch >= 0x2150 && ch <= 0x218F) || // Number forms
      (ch >= 0x2200 && ch <= 0x22FF) || // Mathematical operators
      (ch >= 0x2300 && ch <= 0x231F) || // Misc technical
      (ch >= 0x25A0 && ch <= 0x25FF) || // Geometric shapes
      (ch >= 0x2600 && ch <= 0x26FF) || // Misc symbols
      (ch >= 0x2700 && ch <= 0x27BF) || // Dingbats
      (ch >= 0x2B00 && ch <= 0x2BFF) || // Arrows and symbols
      (ch >= 0x3000 && ch <= 0x303F) || // CJK symbols/punctuation
      (ch >= 0x3040 && ch <= 0x30FF) || // Hiragana/Katakana
      (ch >= 0x31F0 && ch <= 0x31FF) || // Katakana phonetic extensions
      (ch >= 0x3400 && ch <= 0x4DBF) || // CJK extension A`
      (ch >= 0x4E00 && ch <= 0x9FFF) || // CJK unified ideographs
      (ch >= 0xA960 && ch <= 0xA97F) || // Hangul Jamo extended-A
      (ch >= 0xAC00 && ch <= 0xD7AF) || // Hangul syllables
      (ch >= 0xF000 && ch <= 0xF8FF)) { // Private use area
    return true;
  }

  return false;
}

void TerminalView::RenderRowWithGrouping(wxDC &dc, int y, int rowIdx,
                                         const std::vector<terminal::Cell> &row,
                                         const wxRect &selected_cells,
                                         PaintCounters &counters) {
  std::vector<CellInfo> cells =
      PrepareRowForDrawing(row, rowIdx, selected_cells);

  if (cells.empty()) {
    return;
  }

  if (!IsAsciiSafeTextRun(cells)) {
    return RenderRowNoGrouping(dc, y, rowIdx, row, selected_cells, counters);
  }
  counters.grouped_rows_++;
  for (size_t i = 0; i < cells.size();) {
    const auto &firstCell = cells[i];
    wxString text;
    text.Append(firstCell.ch);

    size_t j = i + 1;
    while (j < cells.size() && cells[j].attrs == firstCell.attrs &&
           cells[j].colIdx == cells[j - 1].colIdx + 1) {
      text.Append(cells[j].ch);
      ++j;
    }

    int x = firstCell.colIdx * m_charW;
    int width = (cells[j - 1].colIdx - firstCell.colIdx + 1) * m_charW;
    dc.SetBrush(wxBrush(firstCell.attrs.bgColor));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(x, y, width, m_charH);
    counters.draw_rectangle_++;

    dc.SetTextForeground(firstCell.attrs.fgColor);
    dc.SetFont(GetCachedFont(firstCell.attrs.bold, firstCell.attrs.underline));
    dc.DrawText(text, x, y);
    counters.draw_text_++;

    i = j;
  }
}

void TerminalView::RenderRowNoGrouping(wxDC &dc, int y, int rowIdx,
                                       const std::vector<terminal::Cell> &row,
                                       const wxRect &selected_cells,
                                       PaintCounters &counters) {
  // Build a list of cells with their attributes, skipping truly empty ones
  std::vector<CellInfo> cells =
      PrepareRowForDrawing(row, rowIdx, selected_cells);

  // Now group and render consecutive cells with the same attributes
  if (cells.empty()) {
    return;
  }

  std::optional<CellAttributes> prev_cell{std::nullopt};
  for (size_t i = 0; i < cells.size(); ++i) {
    const auto &cell = cells[i];

    // Draw background for the entire group
    int x = cell.colIdx * m_charW;
    if (!prev_cell.has_value() || prev_cell.value() != cell.attrs) {
      dc.SetBrush(wxBrush(cell.attrs.bgColor));
      dc.SetFont(GetCachedFont(cell.attrs.bold, cell.attrs.underline));
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.SetTextForeground(cell.attrs.fgColor);
      prev_cell = cell.attrs;
    }

    wxString cell_content(wxUniChar(cell.ch));
    dc.DrawRectangle(x, y, m_charW, m_charH);
    counters.draw_rectangle_++;
    dc.DrawText(cell_content, x, y);
    counters.draw_text_++;
  }
}

void TerminalView::RenderRowPosix(wxDC &dc, int y, int rowIdx,
                                  const std::vector<terminal::Cell> &row,
                                  const wxRect &selected_cells,
                                  PaintCounters &counters) {
  std::vector<CellInfo> cells =
      PrepareRowForDrawing(row, rowIdx, selected_cells);

  if (cells.empty()) {
    return;
  }

  // Pass 1: draw background rectangles, grouped by bg color
  dc.SetPen(*wxTRANSPARENT_PEN);
  for (size_t i = 0; i < cells.size();) {
    const auto &firstCell = cells[i];
    size_t j = i + 1;
    while (j < cells.size() &&
           cells[j].attrs.bgColor == firstCell.attrs.bgColor &&
           cells[j].colIdx == cells[j - 1].colIdx + 1) {
      ++j;
    }
    int x = firstCell.colIdx * m_charW;
    int width = (cells[j - 1].colIdx - firstCell.colIdx + 1) * m_charW;
    dc.SetBrush(wxBrush(firstCell.attrs.bgColor));
    dc.DrawRectangle(x, y, width, m_charH);
    counters.draw_rectangle_++;
    i = j;
  }

  // Pass 2: draw text, grouped by font + fg color (ignoring bg differences)
  for (size_t i = 0; i < cells.size();) {
    const auto &firstCell = cells[i];
    wxString text;
    text.Append(firstCell.ch);

    size_t j = i + 1;
    while (j < cells.size() &&
           cells[j].attrs.fgColor == firstCell.attrs.fgColor &&
           cells[j].attrs.bold == firstCell.attrs.bold &&
           cells[j].attrs.underline == firstCell.attrs.underline &&
           cells[j].colIdx == cells[j - 1].colIdx + 1) {
      text.Append(cells[j].ch);
      ++j;
    }

    int x = firstCell.colIdx * m_charW;
    dc.SetTextForeground(firstCell.attrs.fgColor);
    dc.SetFont(GetCachedFont(firstCell.attrs.bold, firstCell.attrs.underline));
    dc.DrawText(text, x, y);
    counters.draw_text_++;

    i = j;
  }
}

#if defined(__WXMAC__)
void TerminalView::MACRenderRow(wxDC &dc, int y, int rowIdx,
                                const std::vector<terminal::Cell> &row,
                                const wxRect &selected_cells,
                                PaintCounters &counters) {
  std::vector<CellInfo> cells =
      PrepareRowForDrawing(row, rowIdx, selected_cells);

  if (cells.empty()) {
    return;
  }

  // Pass 1: draw background rectangles, grouped by bg color
  dc.SetPen(*wxTRANSPARENT_PEN);
  for (size_t i = 0; i < cells.size();) {
    const auto &firstCell = cells[i];
    size_t j = i + 1;
    while (j < cells.size() &&
           cells[j].attrs.bgColor == firstCell.attrs.bgColor &&
           cells[j].colIdx == cells[j - 1].colIdx + 1) {
      ++j;
    }
    int x = firstCell.colIdx * m_charW;
    int width = (cells[j - 1].colIdx - firstCell.colIdx + 1) * m_charW;
    dc.SetBrush(wxBrush(firstCell.attrs.bgColor));
    dc.DrawRectangle(x, y, width, m_charH);
    counters.draw_rectangle_++;
    i = j;
  }

  // Pass 2: draw glyphs at exact grid positions via CTFontDrawGlyphs
  wxGraphicsContext *gc = dc.GetGraphicsContext();
  CGContextRef ctx =
      gc ? static_cast<CGContextRef>(gc->GetNativeContext()) : nullptr;

  if (!ctx) {
    RenderRowNoGrouping(dc, y, rowIdx, row, selected_cells, counters);
    return;
  }

  int dcHeight = dc.GetSize().GetHeight();

  // The DC's CGContext is flipped (Y-down). CTFontDrawGlyphs needs
  // unflipped coordinates. Save state, undo the flip, draw, restore.
  CGContextSaveGState(ctx);
  CGContextScaleCTM(ctx, 1.0, -1.0);
  CGContextTranslateCTM(ctx, 0, -dcHeight);

  for (size_t i = 0; i < cells.size();) {
    const auto &firstCell = cells[i];
    const wxFont &font =
        GetCachedFont(firstCell.attrs.bold, firstCell.attrs.underline);
    CTFontRef ctFont = font.OSXGetCTFont();
    if (!ctFont) {
      ++i;
      continue;
    }

    CGFloat descent = CTFontGetDescent(ctFont);

    std::vector<CGGlyph> glyphs;
    std::vector<CGPoint> positions;

    size_t j = i;
    while (j < cells.size() &&
           cells[j].attrs.fgColor == firstCell.attrs.fgColor &&
           cells[j].attrs.bold == firstCell.attrs.bold &&
           cells[j].attrs.underline == firstCell.attrs.underline) {
      UniChar ch = static_cast<UniChar>(cells[j].ch);
      CGGlyph glyph = 0;
      if (CTFontGetGlyphsForCharacters(ctFont, &ch, &glyph, 1) && glyph) {
        glyphs.push_back(glyph);
        // Now in unflipped CG coords: origin bottom-left, Y goes up
        CGFloat cgX = static_cast<CGFloat>(cells[j].colIdx * m_charW);
        CGFloat cgY = static_cast<CGFloat>(dcHeight - y) - m_charH + descent;
        positions.push_back(CGPointMake(cgX, cgY));
      }
      ++j;
    }

    if (!glyphs.empty()) {
      const wxColour &fg = firstCell.attrs.fgColor;
      CGContextSetRGBFillColor(ctx, fg.Red() / 255.0, fg.Green() / 255.0,
                               fg.Blue() / 255.0, fg.Alpha() / 255.0);
      CTFontDrawGlyphs(ctFont, glyphs.data(), positions.data(), glyphs.size(),
                       ctx);
      counters.draw_text_++;
    }
    i = j;
  }

  CGContextRestoreGState(ctx);
}
#endif

void TerminalView::RenderRow(wxDC &dc, int y, int rowIdx,
                             const std::vector<terminal::Cell> &row,
                             const wxRect &selected_cells,
                             PaintCounters &counters) {
  if (IsSafeDrawing()) {
    // When enabled, we draw cell by cell, it is slower
    // but it can handle unicode and other non aligned grids with
    // accuracy and avoid drawing glitches.
    RenderRowNoGrouping(dc, y, rowIdx, row, selected_cells, counters);
    return;
  }

#if defined(__WXMSW__)
  RenderRowWithGrouping(dc, y, rowIdx, row, selected_cells, counters);
#elif defined(__WXMAC__)
  MACRenderRow(dc, y, rowIdx, row, selected_cells, counters);
#else
  RenderRowPosix(dc, y, rowIdx, row, selected_cells, counters);
#endif
}

void TerminalView::OnPaint(wxPaintEvent &) {
  // For logging purposes
  LogFunction function_logger{"TerminalView::OnPaint",
                              TerminalLogLevel::kTrace};
  PaintCounters paint_counters{
      function_logger.AddCounter("DrawText calls"),
      function_logger.AddCounter("DrawRectangle calls"),
      function_logger.AddCounter("GroupedRows calls"),
  };

  wxAutoBufferedPaintDC dc{this};

  const auto &theme = m_core.GetTheme();
  dc.SetBackground(wxBrush(theme.bg));
  dc.Clear();
  dc.SetFont(m_defaultFont);

  if (m_charH == 0 && m_charW == 0) {
    // first time — measure font, set size, then start the shell
    m_charW = wxMax(dc.GetTextExtent("X").GetWidth(), m_charW);
    m_charH = wxMax(dc.GetTextExtent("X").GetHeight(), m_charH);

    const wxString sample_text = "Administrator";
    int group_width =
        dc.GetTextExtent(sample_text).GetWidth() / sample_text.length();
    TLOG_IF_DEBUG {
      TLOG_DEBUG() << "m_charW=" << m_charW << ", m_charH=" << m_charH
                   << ", group_width=" << group_width << std::endl;
    }
    SetTerminalSizeFromClient();
    if (m_backend == nullptr) {
      CallAfter(&TerminalView::StartProcess, m_shell_command, m_environment);
    }
    // Now that the char width is known, do another paint.
    CallAfter(&TerminalView::Refresh, true, nullptr);
    return;
  }

  m_charW = dc.GetTextExtent("X").GetWidth();
  m_charH = dc.GetTextExtent("X").GetHeight();

  wxRect selected_cells = {};
  if (m_mouseSelectionRect.IsSelectionRectHasMinSize()) {
    selected_cells =
        m_mouseSelectionRect.PixelsRectToViewCellRect(m_charW, m_charH);
  }

  int y = 0;
  auto viewArea = m_core.GetViewArea();
  for (int rowIdx = 0; rowIdx < static_cast<int>(viewArea.size()); ++rowIdx) {
    const auto &row = *viewArea[rowIdx];
    RenderRow(dc, y, rowIdx, row, selected_cells, paint_counters);
    y += m_charH;
  }

  DrawFocusBorder(dc);

  // Draw cursor (block caret) — only if shell
  // viewport is visible
  auto cursor = m_core.Cursor();
  std::size_t viewStart = m_core.ViewStart();
  std::size_t shellStart = m_core.ShellStart();
  if (viewStart <= shellStart &&
      shellStart + cursor.y < viewStart + m_core.Rows()) {
    int screenRow = static_cast<int>(shellStart - viewStart + cursor.y);
    int cx = cursor.x * m_charW;
    int cy = screenRow * m_charH;

    // Draw cursor block
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(theme.cursorColour));
    dc.DrawRectangle(cx, cy, m_charW, m_charH);

    // Draw the character at cursor position with
    // inverted color
    if (screenRow >= 0 && screenRow < static_cast<int>(viewArea.size())) {
      const auto &cursorRow = *viewArea[screenRow];
      if (cursor.x >= 0 && cursor.x < static_cast<int>(cursorRow.size())) {
        const auto &cell = cursorRow[cursor.x];

        // Draw character with inverted cursor
        // color (typically black on white cursor)
        if (cell.ch != U' ') {
          dc.SetTextForeground(theme.bg); // Use background color
                                          // (typically black)
          const wxFont &font = GetCachedFont(cell.bold, cell.underline);
          dc.SetFont(font);
          dc.DrawText(wxString(wxUniChar(cell.ch)), cx, cy);
          dc.SetFont(m_defaultFont);
        }
      }
    }
  }
}

void TerminalView::OnSize(wxSizeEvent &evt) {
  TLOG_DEBUG() << "OnSize: " << GetClientSize().GetWidth() << "x"
               << GetClientSize().GetHeight() << std::endl;
  SetTerminalSizeFromClient();
  m_needsRepaint = false;
  Refresh();
  evt.Skip();
}

void TerminalView::OnMouseLeftDown(wxMouseEvent &evt) {
  evt.Skip();
  if (m_charW == 0 || m_charH == 0) {
    return;
  }

  if (!m_mouseSelectionRect.IsEmpty()) {
    // Clear and start a new selection.
    m_mouseSelectionRect = {};
  }

  m_mouseSelectionRect.SetAnchor(wxPoint{evt.GetX(), evt.GetY()});
  m_isDragging = true;
  m_needsRepaint = false;
  Refresh();
}

void TerminalView::OnMouseMove(wxMouseEvent &evt) {
  evt.Skip();
  if (!m_isDragging) {
    return;
  }

  wxPoint pt2{evt.GetX(), evt.GetY()};
  m_mouseSelectionRect.UpdateCurrent(pt2);
  m_needsRepaint = true;
}

void TerminalView::OnMouseUp(wxMouseEvent &evt) {
  evt.Skip();
  m_isDragging = false;

  if (!m_mouseSelectionRect.IsSelectionRectHasMinSize()) {
    m_mouseSelectionRect.Clear();
    return;
  }

  // Adjust the selection rect into cell rect.
  m_mouseSelectionRect.SnapToCellGrid(m_charW, m_charH);
  m_needsRepaint = false;
  Refresh();
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
  m_mouseSelectionRect = {};
  m_needsRepaint = true;
}

void TerminalView::OnContextMenu(wxContextMenuEvent &evt) {
  wxMenu menu;

  if (m_mouseSelectionRect.IsSelectionRectHasMinSize()) {
    menu.Append(wxID_COPY, _("Copy"));
  }
  menu.Append(wxID_PASTE, _("Paste"));
  menu.AppendSeparator();
  menu.Append(wxID_CLEAR, _("Clear buffer"));

  menu.Bind(wxEVT_MENU, &TerminalView::OnCopy, this, wxID_COPY);
  menu.Bind(wxEVT_MENU, &TerminalView::OnPaste, this, wxID_PASTE);
  menu.Bind(wxEVT_MENU, &TerminalView::OnClearBuffer, this, wxID_CLEAR);

  m_contextMenuShowing = true;
  PopupMenu(&menu);
  m_contextMenuShowing = false;
  evt.Skip();
}

void TerminalView::OnCopy(wxCommandEvent &evt) {
  wxUnusedVar(evt);
  Copy();
}

void TerminalView::OnClearBuffer(wxCommandEvent &evt) {
  wxUnusedVar(evt);
  ClearAll();
}

void TerminalView::OnPaste(wxCommandEvent &evt) {
  wxUnusedVar(evt);
  Paste();
}

void TerminalView::OnFocus(wxFocusEvent &evt) {
  // Ensure we can receive keyboard events
  evt.Skip();
  m_hasFocusBorder = true;
  Refresh();
}

void TerminalView::OnLostFocus(wxFocusEvent &evt) {
  evt.Skip();
  m_hasFocusBorder = false;
  ClearMouseSelection();
  Refresh();
}

void TerminalView::DrawFocusBorder(wxDC &dc) const {
  if (!m_hasFocusBorder) {
    return;
  }

  wxRect r = GetClientRect();
  if (r.width <= 1 || r.height <= 1) {
    return;
  }

  const wxColour bg = GetBackgroundColour();
  const int bgLuminance = (bg.Red() * 299 + bg.Green() * 587 + bg.Blue() * 114) / 1000;
  const wxColour focusRectColour = bgLuminance >= 128 ? bg.ChangeLightness(50)
                                                      : bg.ChangeLightness(150);

  wxPen pen(focusRectColour, 1);
  pen.SetCap(wxCAP_BUTT);
  pen.SetJoin(wxJOIN_MITER);
  dc.SetPen(pen);
  dc.SetBrush(*wxTRANSPARENT_BRUSH);
  dc.DrawRectangle(r.GetX(), r.GetY(), r.GetWidth() - 1, r.GetHeight() - 1);
}

void TerminalView::OnCharHook(wxKeyEvent &evt) {
  if (!HasFocus()) {
    evt.Skip();
    return;
  }

  // This event is sent before the key is processed
  // by the default handlers We intercept
  // navigation keys (Enter, Tab, Escape) here
  int key = evt.GetKeyCode();

  if (key == WXK_RETURN || key == WXK_NUMPAD_ENTER) {
    SendEnter();
    return;
  } else if (key == WXK_TAB) {
    SendTab();
    return;
  } else if (key == WXK_ESCAPE) {
    if (m_mouseSelectionRect.IsSelectionRectHasMinSize()) {
      ClearMouseSelection();
      return;
    }
    SendEscape();
    return;
  }

#ifdef __WXMSW__
  // Special keys should be handled here on
  // Windows.
  if (HandleSpecialKeys(evt)) {
    return;
  }
#endif
  // Let OnKeyDown process this as well.
  evt.Skip();
}

void TerminalView::OnKeyDown(wxKeyEvent &evt) {
  if (!HasFocus()) {
    evt.Skip();
    return;
  }

  const int key = evt.GetKeyCode();

  // Handle Ctrl combinations
  const bool ctrl = evt.RawControlDown();

#ifdef __WXMAC__
  // On macOS, Cmd+C/V for copy/paste
  // (ControlDown() = Cmd key)
  if (evt.ControlDown() && !evt.AltDown()) {
    if (IsSelectionRectHasMinSize(m_mouseSelectionRect) &&
        (key == 'C' || key == 'c')) {
      wxCommandEvent copyEvt(wxEVT_MENU, wxID_COPY);
      OnCopy(copyEvt);
      return;
    }
    if (key == 'V' || key == 'v') {
      wxCommandEvent pasteEvt(wxEVT_MENU, wxID_PASTE);
      OnPaste(pasteEvt);
      return;
    }
  }
#endif

  if (ctrl && !evt.AltDown()) {
    // Handle Ctrl+C - Copy if text is selected,
    // otherwise send SIGINT
    if (key == 'C' || key == 'c') {
      TLOG_DEBUG() << "Sending Ctrl-C" << std::endl;
      SendCtrlC();
      return;
    }

    // Handle Ctrl+U - Clear current line
    // (Unix-style line kill)
    if (key == 'U' || key == 'u') {
      SendCtrlU();
      return;
    }

    if (key == 'L' || key == 'l') {
      SendCtrlL();
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
    SendEscape();
    SendInput(std::string(1, static_cast<char>(key)));
    return;
  }

  // Handle special keys
  // Note: ENTER, TAB, and ESCAPE are handled in
  // OnCharHook
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
  // Note: GetUnicodeKey() doesn't work properly in
  // KEY_DOWN on Windows We need to handle case
  // conversion ourselves
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
  if (m_needsRepaint) {
    m_needsRepaint = false;
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

void TerminalView::Copy() {
  TLOG_DEBUG() << "Copy is called!" << std::endl;
  if (!m_mouseSelectionRect.IsSelectionRectHasMinSize()) {
    TLOG_DEBUG() << "No selection is active - will "
                    "do nothing"
                 << std::endl;
    return;
  }

  // Get the selected text
  wxString selection;
  wxRect rect = m_mouseSelectionRect.PixelsRectToViewCellRect(m_charW, m_charH);
  TLOG_DEBUG() << "Copying content: " << rect << std::endl;
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

  TLOG_DEBUG() << "Copying:" << selection.size() << " chars. Content:\n"
               << selection << std::endl;
  // Copy to clipboard
  if (wxTheClipboard->Open()) {
    wxTheClipboard->SetData(new wxTextDataObject(selection));
    wxTheClipboard->Flush();
    wxTheClipboard->Close();
  }
  ClearMouseSelection();
  m_needsRepaint = true;
}

void TerminalView::Paste() {
  TLOG_DEBUG() << "Paste is called!" << std::endl;
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
}

void TerminalView::ClearAll() {
  Feed("\033[2J\033[3J");
  m_needsRepaint = true;
}
