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

inline bool IsSelectionRectHasMinSize(const wxRect &rect) {
  static constexpr int kMinRectSize = 2;
  return !rect.IsEmpty() && (rect.GetSize().GetWidth() >= kMinRectSize ||
                             rect.GetSize().GetHeight() >= kMinRectSize);
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

TerminalView::TerminalView(wxWindow *parent, const wxString &shellCommand)
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
  Bind(wxEVT_TIMER, &TerminalView::OnTimer, this);
  Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &e) {
    e.Skip();
    if (!m_contextMenuShowing) {
      LOG_DEBUG() << "Leaving window!" << std::endl;
      ClearMouseSelection();
    }
  });
  Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent &) {});
  StartProcess(shellCommand);

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

bool TerminalView::StartProcess(const wxString &command) {
  if (!m_backend) {
    m_backend = terminal::PtyBackend::Create(GetEventHandler());
  }
  m_shell_command = command;
  LOG_DEBUG() << "Starting shell with command: " << command << std::endl;
  return m_backend && m_backend->Start(m_shell_command.ToStdString(wxConvUTF8),
                                       [this](const std::string &out) {
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
  m_mouseSelectionRect = {};
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
  LOG_IF_DEBUG {
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
      LOG_DEBUG() << wxString::Format("%03d", row_num) << line_utf8
                  << std::endl;
      row_num++;
    }
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

wxRect TerminalView::PixelsRectToViewCellRect(const wxRect &pixelrect) const {
  if (m_charH == 0 || m_charW == 0) {
    return {};
  }
  if (pixelrect.IsEmpty()) {
    return {};
  }

  // Convert top-left corner (round down to get the cell that contains this
  // pixel)
  int cell_x = pixelrect.GetX() / m_charW;
  int cell_y = pixelrect.GetY() / m_charH;

  // Convert bottom-right corner (round up to include cells touched by the pixel
  // rect)
  int pixel_right = pixelrect.GetRight();
  int pixel_bottom = pixelrect.GetBottom();
  int cell_right = (pixel_right + m_charW - 1) / m_charW - 1;
  int cell_bottom = (pixel_bottom + m_charH - 1) / m_charH - 1;

  // Calculate width and height
  int cell_width = cell_right - cell_x + 1;
  int cell_height = cell_bottom - cell_y + 1;

  return wxRect(cell_x, cell_y, cell_width, cell_height);
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

void TerminalView::RenderRowNoGrouping(wxDC &dc, int y, int rowIdx,
                                       const std::vector<terminal::Cell> &row,
                                       const wxRect &selected_cells,
                                       size_t &draw_text_calls) {
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
    dc.DrawText(cell_content, x, y);
    draw_text_calls++;
  }
}

void TerminalView::RenderRow(wxDC &dc, int y, int rowIdx,
                             const std::vector<terminal::Cell> &row,
                             const wxRect &selected_cells,
                             size_t &draw_text_calls) {
#ifdef __WXMSW__
  // Build a list of cells with their attributes, skipping truly empty ones
  std::vector<CellInfo> cells =
      PrepareRowForDrawing(row, rowIdx, selected_cells);

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
#else
  RenderRowNoGrouping(dc, y, rowIdx, row, selected_cells, draw_text_calls);
#endif
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

  wxRect selected_cells = {};
  if (IsSelectionRectHasMinSize(m_mouseSelectionRect)) {
    selected_cells = PixelsRectToViewCellRect(m_mouseSelectionRect);
  }

  int y = 0;
  auto viewArea = m_core.GetViewArea();
  for (int rowIdx = 0; rowIdx < static_cast<int>(viewArea.size()); ++rowIdx) {
    const auto &row = *viewArea[rowIdx];
    RenderRow(dc, y, rowIdx, row, selected_cells, draw_text_calls);
    y += m_charH;
  }

  // Draw cursor (block caret) — only if shell viewport is visible
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

    // Draw the character at cursor position with inverted color
    if (screenRow >= 0 && screenRow < static_cast<int>(viewArea.size())) {
      const auto &cursorRow = *viewArea[screenRow];
      if (cursor.x >= 0 && cursor.x < static_cast<int>(cursorRow.size())) {
        const auto &cell = cursorRow[cursor.x];

        // Draw character with inverted cursor color (typically black on white
        // cursor)
        if (cell.ch != U' ') {
          dc.SetTextForeground(
              theme.bg); // Use background color (typically black)
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
  SetTerminalSizeFromClient();
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

  m_mouseSelectionRect = wxRect(evt.GetX(), evt.GetY(), 1, 1);
  m_isDragging = true;
  m_dirty = true;
}

void TerminalView::OnMouseMove(wxMouseEvent &evt) {
  evt.Skip();
  if (!m_isDragging) {
    return;
  }

  wxPoint pt2{evt.GetX(), evt.GetY()};
  m_mouseSelectionRect = MakeRect(m_mouseSelectionRect.GetTopLeft(), pt2);
  m_dirty = true;
}

void TerminalView::OnMouseUp(wxMouseEvent &evt) {
  evt.Skip();
  m_isDragging = false;

  if (!IsSelectionRectHasMinSize(m_mouseSelectionRect)) {
    m_mouseSelectionRect = {};
    return;
  }

  // Adjust the selection rect into cell rect.
  wxRect cell_based_rect = PixelsRectToViewCellRect(m_mouseSelectionRect);
  m_mouseSelectionRect = ViewCellToPixelsRect(cell_based_rect);
  m_dirty = true;
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
  m_dirty = true;
}

void TerminalView::OnContextMenu(wxContextMenuEvent &evt) {
  wxMenu menu;

  if (IsSelectionRectHasMinSize(m_mouseSelectionRect)) {
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
  LOG_DEBUG() << "Copy is called!" << std::endl;
  if (!IsSelectionRectHasMinSize(m_mouseSelectionRect)) {
    LOG_DEBUG() << "No selection is active - will do nothing" << std::endl;
    return;
  }

  // Get the selected text
  wxString selection;
  wxRect rect = PixelsRectToViewCellRect(m_mouseSelectionRect);
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

void TerminalView::OnClearBuffer(wxCommandEvent &evt) {
  wxUnusedVar(evt);
  Feed("\033[2J\033[3J");
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
    if (IsSelectionRectHasMinSize(m_mouseSelectionRect)) {
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

#ifdef __WXMAC__
  // On macOS, Cmd+C/V for copy/paste (ControlDown() = Cmd key)
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
    // Handle Ctrl+C - Copy if text is selected, otherwise send SIGINT
    if (key == 'C' || key == 'c') {
      LOG_DEBUG() << "Sending Ctrl-C" << std::endl;
      SendCtrlC();
      return;
    }

    // Handle Ctrl+U - Clear current line (Unix-style line kill)
    if (key == 'U' || key == 'u') {
#ifdef __WXMSW__
      if (m_shell_command.empty()) {
        SendEscape();
        return;
      }
#endif
      // User is using a custom shell on Windows or non Windows code, anyways,
      // use the standard
      SendInput(std::string(1, '\x15'));
      return;
    }

#ifdef __WXMSW__
    if (m_shell_command.empty() && key == 'L' || key == 'l') {
      // Windows style "Ctrl-L" for CMD / PS
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
