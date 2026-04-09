#include "terminal_view.h"

#include "terminal_event.h"
#include "terminal_logger.h"
#include <algorithm>
#if USE_TIMER_REFRESH
#include <chrono>
#endif
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <wx/app.h>
#include <wx/clipbrd.h>
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/font.h>
#include <wx/frame.h>
#include <wx/gdicmn.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/window.h>

using terminal::ColourSpec;

#ifndef __WXMSW__
extern char **environ;
#endif

namespace {
using EnvMap = std::unordered_map<wxString, wxString>;
/**
 * @brief Builds a map of the current process environment variables.
 *
 * Scans the platform-provided environment block and copies each "name=value"
 * pair into a new EnvMap. On Unix-like systems, keys and values are converted
 * from UTF-8 into wxString before insertion; on Windows, wide-character strings
 * are used directly.
 *
 * @return EnvMap A map containing the environment variables available to the
 *         current process. If the environment cannot be accessed, an empty map
 *         is returned.
 */
EnvMap BuildEnvMap() {
  EnvMap env_map;

#ifdef _WIN32
  // Windows implementation
  LPWCH env_strings = GetEnvironmentStrings();
  if (env_strings != nullptr) {
    LPWCH current = env_strings;
    while (*current != '\0') {
      std::wstring env_entry(current);
      size_t eq_pos = env_entry.find('=');
      if (eq_pos != std::wstring::npos && eq_pos > 0) {
        std::wstring key = env_entry.substr(0, eq_pos);
        std::wstring value = env_entry.substr(eq_pos + 1);
        env_map[key] = value;
      }
      current += env_entry.size() + 1;
    }
    FreeEnvironmentStrings(env_strings);
  }
#else
  // Unix-like systems
  if (environ != nullptr) {
    for (char **env = environ; *env != nullptr; ++env) {
      std::string env_entry(*env);
      size_t eq_pos = env_entry.find('=');
      if (eq_pos != std::string::npos) {
        std::string key = env_entry.substr(0, eq_pos);
        std::string value = env_entry.substr(eq_pos + 1);
        env_map[wxString::FromUTF8(key)] = wxString::FromUTF8(value);
      }
    }
  }
#endif

  return env_map;
}

} // namespace

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

wxTerminalViewCtrl::wxTerminalViewCtrl(
    wxWindow *parent, const wxString &shellCommand,
    const std::optional<EnvironmentList> &environment)
    : wxPanel(parent, wxID_ANY) {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  UpdateFontCache();
  SetSelectionDelimChars(" \t<>{}[]()$,;*!@^\"'");

#if USE_TIMER_REFRESH
  // Use thread to trigger paint events (if needed).
  // This is more accurate then using wxTimer.
  m_drawingTimerThread = std::make_unique<std::thread>([this]() {
    while (!m_shutdownFlag.load()) {
      // Check for paint event every 10ms, as best this will render the screen
      // at a 100HZ rate.
      std::this_thread::sleep_for(std::chrono::milliseconds{10});
      if (m_needsRepaint.load()) {
        m_needsRepaint.store(false);
        CallAfter(&wxTerminalViewCtrl::Refresh, true, nullptr);
      }
    }
  });
#endif

  // Bind events using modern API
  Bind(wxEVT_PAINT, &wxTerminalViewCtrl::OnPaint, this);
  Bind(wxEVT_SIZE, &wxTerminalViewCtrl::OnSize, this);
  Bind(wxEVT_CHAR_HOOK, &wxTerminalViewCtrl::OnCharHook, this);
  Bind(wxEVT_KEY_DOWN, &wxTerminalViewCtrl::OnKeyDown, this);
  Bind(wxEVT_LEFT_DOWN, &wxTerminalViewCtrl::OnMouseLeftDown, this);
  Bind(wxEVT_LEFT_DCLICK, &wxTerminalViewCtrl::OnMouseLeftDoubleClick, this);
  Bind(wxEVT_LEFT_UP, &wxTerminalViewCtrl::OnMouseUp, this);
  Bind(wxEVT_MOTION, &wxTerminalViewCtrl::OnMouseMove, this);
  Bind(wxEVT_CONTEXT_MENU, &wxTerminalViewCtrl::OnContextMenu, this);
  Bind(wxEVT_MOUSEWHEEL, &wxTerminalViewCtrl::OnMouseWheel, this);
  Bind(wxEVT_SET_FOCUS, &wxTerminalViewCtrl::OnFocus, this);
  Bind(wxEVT_KILL_FOCUS, &wxTerminalViewCtrl::OnLostFocus, this);
  Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &e) { e.Skip(); });
  Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent &) {});

  m_shell_command = shellCommand;
  m_environment = std::move(environment);

  if (!m_environment.has_value()) {
    TLOG_DEBUG() << "No env provided. Building one." << std::endl;
    // If no env provided, create one.
    EnvironmentList env_list;
    auto env_map = BuildEnvMap();
    for (const auto &[n, v] : env_map) {
      wxString entry;
      entry << n << "=" << v;
      TLOG_DEBUG() << "    " << entry << std::endl;
      env_list.push_back(entry.ToStdString(wxConvUTF8));
    }
    m_environment = env_list;
  }

  // Set up callback for terminal responses (e.g., cursor position reports)
  m_core.SetResponseCallback(
      [this](const std::string &response) { SendInput(response); });
  m_core.SetTitleCallback([this](const std::string &title) {
    wxTerminalEvent event{wxEVT_TERMINAL_TITLE_CHANGED};
    event.SetTitle(wxString::FromUTF8(title));
    event.SetEventObject(this);
    GetEventHandler()->AddPendingEvent(event);
  });
  m_core.SetBellCallback([this]() {
    wxTerminalEvent event{wxEVT_TERMINAL_BELL};
    event.SetEventObject(this);
    GetEventHandler()->AddPendingEvent(event);
  });
  CallAfter(&wxTerminalViewCtrl::SetFocus);
}

wxTerminalViewCtrl::~wxTerminalViewCtrl() {
#if USE_TIMER_REFRESH
  m_shutdownFlag.store(true);
  if (m_drawingTimerThread && m_drawingTimerThread->joinable()) {
    m_drawingTimerThread->join();
  }
  m_shutdownFlag.store(false);
#endif
  if (m_backend) {
    m_backend->Stop();
  }
}

void wxTerminalViewCtrl::Feed(const std::string &data) {
  m_core.PutData(data);
  m_core.SetViewStart(m_core.ShellStart());
  RefreshView();
}

void wxTerminalViewCtrl::SetTerminalSizeFromClient() {
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

void wxTerminalViewCtrl::StartProcess(
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
                               CallAfter(&wxTerminalViewCtrl::Feed, out);
                             });
  if (ok && m_core.Cols() > 0 && m_core.Rows() > 0) {
    m_backend->Resize(static_cast<int>(m_core.Cols()),
                      static_cast<int>(m_core.Rows()));
  }
}

void wxTerminalViewCtrl::SendInput(const std::string &text) {
  if (m_backend) {
    m_backend->Write(text);
  }
}

// Helper methods for sending special characters
void wxTerminalViewCtrl::SendCtrlC() {
#if defined(__WXGTK__) || defined(__WXMSW__)
  if (HasActiveSelection()) {
    Copy();
    return;
  }
#endif
  if (m_backend) {
    m_backend->SendBreak();
  }
}

void wxTerminalViewCtrl::SendEnter() { SendInput("\r"); }

void wxTerminalViewCtrl::SendTab() { SendInput("\t"); }

void wxTerminalViewCtrl::SendEscape() { SendInput("\x1b"); }

void wxTerminalViewCtrl::SendBackspace() { SendInput("\x7f"); }

void wxTerminalViewCtrl::SendArrowUp() { SendInput("\x1b[A"); }

void wxTerminalViewCtrl::SendArrowDown() { SendInput("\x1b[B"); }

void wxTerminalViewCtrl::SendArrowRight() { SendInput("\x1b[C"); }

void wxTerminalViewCtrl::SendArrowLeft() { SendInput("\x1b[D"); }

void wxTerminalViewCtrl::SendHome() { SendInput("\x1b[H"); }

void wxTerminalViewCtrl::SendEnd() { SendInput("\x1b[F"); }

void wxTerminalViewCtrl::SendDelete() { SendInput("\x1b[3~"); }

void wxTerminalViewCtrl::SendInsert() { SendInput("\x1b[2~"); }

void wxTerminalViewCtrl::SendPageUp() { SendInput("\x1b[5~"); }

void wxTerminalViewCtrl::SendPageDown() { SendInput("\x1b[6~"); }

void wxTerminalViewCtrl::SendCtrlR() {
  if (IsCmdShell()) {
    // send F8
    SendInput("\x1B[19~");
    return;
  }

  // PowerShell & UNIX like terminals
  SendInput("\x12");
}

void wxTerminalViewCtrl::SendCtrlU() {

  if (!IsUnixKeyboardMode()) {
    // Windows style "Ctrl-U" for CMD / PS
    SendEscape();
    return;
  }
  SendInput("\x15");
}

void wxTerminalViewCtrl::SendCtrlL() {
  m_mouseSelection.Clear();
  m_userSelection.Clear();
  if (!IsUnixKeyboardMode()) {
    // Windows style "Ctrl-L" for CMD / PS
    TLOG_DEBUG() << "Sending Windows 'cls' command" << std::endl;
    SendCtrlC();
    if (IsCmdShell()) {
      // Fix the terminal from any unwanted state it was left by sending a RESET
      SendInput("\x1b[0m\r");
    }
    SendCommand("cls");
    return;
  }
  SendInput("\x0c");
}

void wxTerminalViewCtrl::SendCtrlD() {
  if (!IsUnixKeyboardMode()) {
    TLOG_DEBUG() << "Logout using 'exit'" << std::endl;
    SendCtrlC();
    SendCommand("exit");
    return;
  }
  SendInput("\x04");
}

void wxTerminalViewCtrl::SendCtrlW() {
  if (!IsUnixKeyboardMode()) {
    SendInput("\x08"); // Ctrl-BACKSPACE
    return;
  }
  SendInput("\x17");
}

void wxTerminalViewCtrl::SendCtrlZ() { SendInput("\x1a"); }

void wxTerminalViewCtrl::SendCtrlK() {
  if (!IsUnixKeyboardMode()) {
    // Windows shells typically use a command-line kill behavior via Escape.
    SendEscape();
    return;
  }
  SendInput("\x0b");
}

void wxTerminalViewCtrl::SendCtrlA() {
  if (!IsUnixKeyboardMode()) {
    // Windows shells often interpret Ctrl-A as Select All rather than
    // beginning-of-line editing.
    SendHome();
    return;
  }
  SendInput("\x01");
}

void wxTerminalViewCtrl::SendCtrlE() {
  if (!IsUnixKeyboardMode()) {
    // No special Windows equivalent; fall back to the raw control code.
    SendEnd();
    return;
  }
  SendInput("\x05");
}

void wxTerminalViewCtrl::SendAltB() {
  if (!IsUnixKeyboardMode()) {
    // Use a common readline-style escape sequence only when supported.
    SendInput("\x1b"
              "b");
    return;
  }
  SendInput("\x1b"
            "b");
}

void wxTerminalViewCtrl::SendAltF() {
  if (!IsUnixKeyboardMode()) {
    // Use a common readline-style escape sequence only when supported.
    SendInput("\x1b"
              "f");
    return;
  }
  SendInput("\x1b"
            "f");
}

wxString wxTerminalViewCtrl::GetText() const { return m_core.Flatten(); }

void wxTerminalViewCtrl::SetTheme(const wxTerminalTheme &theme) {
  m_core.SetTheme(theme);
  UpdateFontCache();
  m_charH = m_charW = 0; // This needs to be recalculated based on the new font.
  Refresh();
  PostSizeEvent();
}

const wxTerminalTheme &wxTerminalViewCtrl::GetTheme() const {
  return m_core.GetTheme();
}

void wxTerminalViewCtrl::ScrollToLastLine() {
  m_core.SetViewStart(m_core.ShellStart());
  RefreshView();
}

std::size_t wxTerminalViewCtrl::GetLineCount() const {
  return m_core.TotalLines();
}

void wxTerminalViewCtrl::SetBufferSize(std::size_t maxLines) {
  m_core.SetMaxLines(maxLines);
}

std::size_t wxTerminalViewCtrl::GetBufferSize() const {
  return m_core.MaxLines();
}

void wxTerminalViewCtrl::CenterLine(std::size_t line) {
  std::size_t half = m_core.Rows() / 2;
  std::size_t vs = (line > half) ? line - half : 0;
  m_core.SetViewStart(vs);
  RefreshView();
}

wxString wxTerminalViewCtrl::GetViewLine(std::size_t line) const {
  auto abs_line = m_core.ViewStart() + line;
  return GetLine(abs_line);
}

wxString wxTerminalViewCtrl::GetLine(std::size_t line) const {
  const auto &row = m_core.BufferRow(line);
  wxString result;
  result.reserve(row.size());
  for (const auto &cell : row)
    result += static_cast<wxChar>(cell.ch);
  result.Trim();
  return result;
}

void wxTerminalViewCtrl::SetUserSelection(std::size_t row, std::size_t col,
                                          std::size_t count) {
  if (row >= m_core.TotalLines() || col >= m_core.Cols() || count == 0) {
    ClearUserSelection();
    return;
  }
  std::size_t endCol = std::min(col + count, m_core.Cols());
  m_userSelection = {
      .row = row,
      .col = col,
      .endCol = endCol,
      .active = true,
  };
  RefreshView();
}

void wxTerminalViewCtrl::ClearUserSelection() {
  m_userSelection.Clear();
  RefreshView();
  TLOG_IF_DEBUG { TLOG_DEBUG() << "User selection is cleared" << std::endl; }
}

void wxTerminalViewCtrl::ClearMouseSelection() {
  m_mouseSelection.Clear();
  m_isDragging = false;
  RefreshView();
}

bool wxTerminalViewCtrl::HasActiveSelection() const {
  return m_mouseSelection.HasSelection();
}

std::optional<wxPoint>
wxTerminalViewCtrl::PointToCell(const wxPoint &pt) const {
  if (m_charW <= 0 || m_charH <= 0 || pt.x < 0 || pt.y < 0) {
    return std::nullopt;
  }
  return wxPoint{std::max(pt.x, 0) / m_charW, std::max(pt.y, 0) / m_charH};
}

void wxTerminalViewCtrl::UpdateFontCache() {
  m_defaultFont = m_core.GetTheme().font;
  m_defaultFontBold = m_defaultFont;
  m_defaultFontBold.MakeBold();

  m_defaultFontUnderlined = m_defaultFont;
  m_defaultFontUnderlined.MakeUnderlined();

  m_defaultFontBoldUnderlined = m_defaultFont;
  m_defaultFontBoldUnderlined.MakeBold();
  m_defaultFontBoldUnderlined.MakeUnderlined();
}

void wxTerminalViewCtrl::DebugDumpViewArea(TerminalLogLevel log_level,
                                           int viewLine) {
  TLOG_IF(log_level) {
    auto viewArea = m_core.GetViewArea();
    size_t row_num{0};
    for (const auto &row : viewArea) {
      if (viewLine != -1 && viewLine != static_cast<int>(row_num)) {
        row_num++;
        continue;
      }
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
      TLOG(log_level) << wxString::Format("%03d", row_num) << line_utf8
                      << std::endl;
      row_num++;
    }
  }
}

wxColour
wxTerminalViewCtrl::GetColourFromTheme(std::optional<terminal::ColourSpec> spec,
                                       bool foreground) const {
  const auto &theme = m_core.GetTheme();
  if (!spec.has_value() || spec->kind == ColourSpec::Kind::Default)
    return foreground ? theme.fg : theme.bg;
  switch (spec->kind) {
  case ColourSpec::Kind::Ansi:
    return terminal::ToColour(
        theme.GetAnsiColor(spec->ansi.index, spec->ansi.bright));
  case ColourSpec::Kind::Palette256:
    return terminal::ToColour(theme.Get256Color(spec->paletteIndex));
  case ColourSpec::Kind::TrueColor:
    return terminal::ToColour(spec->rgb);
  case ColourSpec::Kind::Default:
    return foreground ? theme.fg : theme.bg;
  }
  return foreground ? theme.fg : theme.bg;
}

wxTerminalViewCtrl::PrepareRowForDrawingResult
wxTerminalViewCtrl::PrepareRowForDrawing(const std::vector<terminal::Cell> &row,
                                         int rowIdx) {
  const auto &theme = m_core.GetTheme();
  // Build a list of cells with their attributes, skipping truly empty ones
  wxTerminalViewCtrl::PrepareRowForDrawingResult result;
  std::vector<CellInfo> &cells = result.cells;
  cells.reserve(row.size());

  // Collect drawable cells
  for (int colIdx = 0; colIdx < static_cast<int>(row.size()); ++colIdx) {
    const auto &cell = row[colIdx];

    if (result.is_ascii_safe) {
      result.is_ascii_safe = IsUnicodeSingleCellSafe(cell.ch);
    }

    // Check if cell is selected
    bool isApiSelected = false;
    if (m_userSelection.active) {
      std::size_t absRow = m_core.ViewStart() + rowIdx;
      isApiSelected =
          (absRow == m_userSelection.row &&
           static_cast<std::size_t>(colIdx) >= m_userSelection.col &&
           static_cast<std::size_t>(colIdx) < m_userSelection.endCol);
    }

    bool isMouseSelected = m_mouseSelection.Contains(colIdx, rowIdx);

    if (!result.row_has_selection) {
      result.row_has_selection = isMouseSelected || isApiSelected;
    }

    // Skip empty cells unless they are selected
    if (cell.IsEmpty() && !isApiSelected && !isMouseSelected) {
      continue;
    }

    // Determine colors
    wxColour bgColor = GetColourFromTheme(
        cell.colours ? cell.colours->bg : std::nullopt, false);
    wxColour fgColor = GetColourFromTheme(
        cell.colours ? cell.colours->fg : std::nullopt, true);

    if (cell.IsReverse()) {
      std::swap(bgColor, fgColor);
    }

    CellInfo info;
    info.colIdx = colIdx;
    info.ch = static_cast<wxChar>(cell.ch);
    if (isMouseSelected) {
      info.attrs.bgColor = theme.selectionBg;
      info.attrs.fgColor = theme.selectionFg;
    } else if (isApiSelected) {
      info.attrs.bgColor = theme.highlightBg;
      info.attrs.fgColor = theme.highlightFg;
    } else {
      info.attrs.bgColor = bgColor;
      info.attrs.fgColor = fgColor;
    }
    info.attrs.bold = cell.IsBold();
    info.attrs.underline = cell.IsUnderlined();
    info.attrs.isClicked = cell.IsClicked();
    info.attrs.isMouseSelected = isMouseSelected;
    info.attrs.isApiSelected = isApiSelected;
    cells.push_back(info);
  }
  return result;
}

bool wxTerminalViewCtrl::IsUnicodeSingleCellSafe(wxChar ch) const {
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

void wxTerminalViewCtrl::PrepareDcForTextDrawing(
    wxDC &dc, const wxTerminalViewCtrl::CellInfo &cell) {
  if (cell.IsOk()) {
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(cell.attrs.bgColor));
    dc.SetFont(GetCachedFont(cell.attrs.bold,
                             cell.attrs.underline || cell.attrs.isClicked));
    dc.SetTextForeground(cell.attrs.fgColor);
  } else {
    // Use theme colours
    const auto &theme = m_core.GetTheme();
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(theme.bg);
    dc.SetFont(m_defaultFont);
    dc.SetTextForeground(theme.fg);
  }
}

void wxTerminalViewCtrl::RenderMonotonicRow(
    wxDC &dc, int y, int rowIdx,
    const std::vector<wxTerminalViewCtrl::CellInfo> &cells,
    PaintCounters &counters) {
  if (cells.empty()) {
    return;
  }
  wxString text;
  int expected_length = cells.back().colIdx - cells.front().colIdx + 1;
  text.reserve(expected_length);

  // Build text, filling gaps with spaces
  int current_col = cells[0].colIdx;
  for (const auto &cell : cells) {
    // Fill any gaps before this cell with spaces
    while (current_col < cell.colIdx) {
      text.Append(' ');
      current_col++;
    }
    // Add the actual cell character
    text.Append(cell.ch);
    current_col++;
  }

  // Calculate bounding rectangle for entire row of cells
  int x_start = cells.front().colIdx * m_charW;
  int x_end = (cells.back().colIdx + 1) * m_charW;
  int width = x_end - x_start;

  // Set DC state once for entire row
  PrepareDcForTextDrawing(dc, cells[0]);
  dc.DrawRectangle(x_start, y, width, m_charH);
  counters.draw_rectangle_++;

  dc.DrawText(text, x_start, y);
  counters.draw_text_++;
  counters.grouped_rows_++;
  counters.full_row_draws_++;
}

void wxTerminalViewCtrl::RenderRowWithGrouping(
    wxDC &dc, int y, int rowIdx, const std::vector<terminal::Cell> &row,
    PaintCounters &counters) {

  auto result = PrepareRowForDrawing(row, rowIdx);
  auto &cells = result.cells;
  if (cells.empty()) {
    return;
  }

  bool isCursorLine = m_core.Cursor().y == rowIdx;
  if (isCursorLine // Never group the cursor line
                   //      || !result.is_ascii_safe    // Found at least 1 cell
                   //      with non safe ascii
      || result.row_has_selection // Found at least 1 cell which is "selected"
  ) {
    return RenderRowNoGrouping(dc, y, rowIdx, row, counters);
  }

  // Fast path: Check if all cells have identical attributes
  // This is common for blank lines, homogeneous backgrounds, etc.
  if (cells.size() > 1) {
    bool all_same_attrs = true;
    const CellAttributes &first_attrs = cells[0].attrs;

    for (size_t i = 1; i < cells.size(); ++i) {
      if (!cells[i].HasSameAttributes(cells[0])) {
        all_same_attrs = false;
        break;
      }
    }

    if (all_same_attrs) {
      // Fast path
      RenderMonotonicRow(dc, y, rowIdx, cells, counters);
      return;
    }
  }

  // Standard path: Group cells with same attributes
  counters.grouped_rows_++;
  std::vector<CellInfo> complete_list;
  complete_list.reserve(cells.back().colIdx + 1);
  complete_list.resize(cells.back().colIdx + 1);

  for (const auto &c : cells) {
    complete_list[c.colIdx] = c;
  }

  auto DrawChunk = [this](wxDC &dc, int y, const wxString &text,
                          const CellInfo &cell, int xx,
                          PaintCounters &counters) -> int {
    // Draw the content.
    PrepareDcForTextDrawing(dc, cell);
    int width = dc.GetTextExtent(text).GetWidth();
    dc.DrawRectangle(xx, y, width, m_charH);
    counters.draw_rectangle_++;

    dc.DrawText(text, xx, y);
    counters.draw_text_++;
    return width;
  };

  int xx = 0;
  wxString text;
  text.reserve(cells.size()); // Max size
  const CellInfo *chunk_first_cell{nullptr};
  for (const auto &c : complete_list) {
    if (chunk_first_cell == nullptr || chunk_first_cell->HasSameAttributes(c)) {
      // Either drawing the first cell or the prev cell and the current one
      // are matching - keep collecting.
      text.Append(c.ch);
      if (chunk_first_cell == nullptr) {
        chunk_first_cell = &c;
      }
      continue;
    }

    // Draw the content.
    xx += DrawChunk(dc, y, text, *chunk_first_cell, xx, counters);
    chunk_first_cell = &c;

    // Keep batching new group
    text.Clear();
    text.Append(c.ch);
  }

  if (!text.empty() && chunk_first_cell) {
    xx += DrawChunk(dc, y, text, *chunk_first_cell, xx, counters);
    text.Clear();
  }
}

void wxTerminalViewCtrl::RenderRowNoGrouping(
    wxDC &dc, int y, int rowIdx, const std::vector<terminal::Cell> &row,
    PaintCounters &counters) {
  // Build a list of cells with their attributes, skipping truly empty ones
  auto result = PrepareRowForDrawing(row, rowIdx);
  auto &cells = result.cells;

  // Now group and render consecutive cells with the same attributes
  if (cells.empty()) {
    return;
  }

  const auto &theme = m_core.GetTheme();
  std::optional<CellAttributes> prev_cell{std::nullopt};
  for (size_t i = 0; i < cells.size(); ++i) {
    const auto &cell = cells[i];

    int x = cell.colIdx * m_charW;
    if (!prev_cell.has_value() || prev_cell.value() != cell.attrs) {
      PrepareDcForTextDrawing(dc, cell);
      prev_cell = cell.attrs;
    }

    wxString cell_content(wxUniChar(cell.ch));
    wxRect cell_rect{x, y, m_charW, m_charH};
    dc.DrawRectangle(cell_rect);
    if (theme.isMonospaced) {
      // Incase of non-monospced font, draw each character at the center of the
      // cell rect.
      counters.draw_rectangle_++;
      dc.DrawText(cell_content, x, y);
    } else {
      wxRect text_rect{wxPoint{x, y}, dc.GetTextExtent(cell_content)};
      text_rect = text_rect.CentreIn(cell_rect, wxHORIZONTAL);
      dc.DrawText(cell_content, text_rect.GetTopLeft());
    }
    counters.draw_text_++;
  }
}

void wxTerminalViewCtrl::RenderRowPosix(wxDC &dc, int y, int rowIdx,
                                        const std::vector<terminal::Cell> &row,
                                        PaintCounters &counters) {
  auto result = PrepareRowForDrawing(row, rowIdx);
  auto &cells = result.cells;

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
    while (j < cells.size() && cells[j].HasSameAttributes(firstCell) &&
           cells[j].IsAdjacent(cells[j - 1])) {
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

void wxTerminalViewCtrl::RenderRow(wxDC &dc, int y, int rowIdx,
                                   const std::vector<terminal::Cell> &row,
                                   PaintCounters &counters) {
  if (IsSafeDrawing()) {
    // When enabled, we draw cell by cell, it is slower
    // but it can handle unicode and other non aligned grids with
    // accuracy and avoid drawing glitches.
    RenderRowNoGrouping(dc, y, rowIdx, row, counters);
    return;
  }
  RenderRowWithGrouping(dc, y, rowIdx, row, counters);
}

void wxTerminalViewCtrl::OnPaint(wxPaintEvent &) {
  // For logging purposes
  LogFunction function_logger{"TerminalView::OnPaint",
                              TerminalLogLevel::kTrace};
  PaintCounters paint_counters{
      function_logger.AddCounter("DrawText calls"),
      function_logger.AddCounter("DrawRectangle calls"),
      function_logger.AddCounter("GroupedRows calls"),
      function_logger.AddCounter("FullRow Grouped"),
  };

  wxAutoBufferedPaintDC dc{this};

  const auto &theme = m_core.GetTheme();
  dc.SetBackground(wxBrush(theme.bg));
  dc.Clear();
  dc.SetFont(m_defaultFont);

  if (m_charH == 0 && m_charW == 0) {
    dc.SetFont(m_defaultFontBold);

    // first time — measure font, set size, then start the shell
    int i_width = dc.GetTextExtent("i").GetWidth();
    m_charW = dc.GetTextExtent("W").GetWidth();

    int i_height = dc.GetTextExtent("i").GetHeight();
    m_charH = dc.GetTextExtent("W").GetHeight();

    if (i_width != m_charW || i_height != m_charH) {
      m_charW = 0;
      // Mark the font has "non-monospaced" font.
      m_core.GetTheme().isMonospaced = false;
      // Find the widest letter from the A-Z
      for (auto c = 'A'; c <= 'Z'; c++) {
        wxString t(c, 1);
        int tmp_width = dc.GetTextExtent(t).GetWidth();
        m_charW = std::max(tmp_width, m_charW);
      }
    } else {
      // Monospaced font.
      static const wxString kTextSample = "codelite/.build-release";
      // Sometimes drawing block of text shows a different char width overall.
      // So use an avg.
      int char_width = m_charW;
      m_charW = std::ceil(
          static_cast<float>(dc.GetTextExtent(kTextSample).GetWidth()) /
          static_cast<float>(kTextSample.length()));

      TLOG_IF_DEBUG {
        TLOG_DEBUG() << "m_charW=" << m_charW << ", m_charH=" << m_charH
                     << ". Single char width is=" << char_width << std::endl;
      }
    }

    SetTerminalSizeFromClient();
    if (m_backend == nullptr) {
      CallAfter(&wxTerminalViewCtrl::StartProcess, m_shell_command,
                m_environment);
    }
    // Now that the char width is known, do another paint.
    CallAfter(&wxTerminalViewCtrl::Refresh, true, nullptr);
    return;
  }

  m_charW = dc.GetTextExtent("X").GetWidth();
  m_charH = dc.GetTextExtent("X").GetHeight();

  // Invalidate selection if the view has scrolled since it was created
  if (m_mouseSelection.HasSelection() &&
      m_mouseSelection.viewStart != m_core.ViewStart()) {
    m_mouseSelection.Clear();
  }

  int y = 0;
  auto viewArea = m_core.GetViewArea();
  for (int rowIdx = 0; rowIdx < static_cast<int>(viewArea.size()); ++rowIdx) {
    const auto &row = *viewArea[rowIdx];
    RenderRow(dc, y, rowIdx, row, paint_counters);
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
          const wxFont &font =
              GetCachedFont(cell.IsBold(), cell.IsUnderlined());
          dc.SetFont(font);
          dc.DrawText(wxString(wxUniChar(cell.ch)), cx, cy);
          dc.SetFont(m_defaultFont);
        }
      }
    }
  }
}

void wxTerminalViewCtrl::OnSize(wxSizeEvent &evt) {
  TLOG_DEBUG() << "OnSize: " << GetClientSize().GetWidth() << "x"
               << GetClientSize().GetHeight() << std::endl;
  SetTerminalSizeFromClient();
  RefreshView(true);
  evt.Skip();
}

void wxTerminalViewCtrl::OnMouseLeftDown(wxMouseEvent &evt) {
  evt.Skip();

  // Check if Control/CMD is down (we use CMD on macOS, since Ctrl+CLICK will
  // show the context menu)
  if (evt.GetModifiers() == wxMOD_CONTROL) {
    m_isDragging = false;
    DoClickable(evt, false);
    RefreshView(true);
    return;
  }

  if (m_charW == 0 || m_charH == 0) {
    ClearMouseSelection();
    return;
  }

  m_mouseSelection.Clear();

  // Convert pixel position to cell coordinates for the anchor
  auto anchorCell = PointToCell(wxPoint{evt.GetX(), evt.GetY()});
  if (!anchorCell.has_value()) {
    return;
  }

  m_mouseSelection.anchor = anchorCell.value();
  m_mouseSelection.current = anchorCell.value();
  m_mouseSelection.viewStart = m_core.ViewStart();
  m_mouseSelection.active = false; // becomes active only when mouse moves to a different cell
  m_isDragging = true;
  RefreshView(true);
  CaptureMouse();
}

void wxTerminalViewCtrl::OnMouseMove(wxMouseEvent &evt) {
  evt.Skip();

  if (!::wxGetKeyState(WXK_RAW_CONTROL)) {
    // Clear any clicked range
    if (m_core.ClearClickedRange()) {
      // We cleared a previously clicked range, refresh is needed.
      RefreshView();
      // Restore the cursor
      SetCursor(wxCURSOR_IBEAM);
    }
  }

  if (!m_isDragging) {
    return;
  }

  auto cell = PointToCell(wxPoint{evt.GetX(), evt.GetY()});
  if (cell.has_value()) {
    if (!m_mouseSelection.active && cell.value() != m_mouseSelection.anchor) {
      m_mouseSelection.active = true;
    }
    m_mouseSelection.current = cell.value();
  }
  RefreshView();
}

void wxTerminalViewCtrl::OnMouseUp(wxMouseEvent &evt) {
  evt.Skip();
  m_isDragging = false;
  if (HasCapture()) {
    // Release the moouse
    ReleaseMouse();
  }

  // Check if Control/CMD is down (we use CMD on macOS, since Ctrl+CLICK will
  // show the context menu)
  if (evt.GetModifiers() == wxMOD_CONTROL) {
    // Control down
    DoClickable(evt, true);
    m_core.ClearClickedRange();
    RefreshView(true);
    return;
  }

  if (!m_mouseSelection.HasSelection()) {
    m_mouseSelection.Clear();
    return;
  }

  RefreshView(true);
}

void wxTerminalViewCtrl::OnMouseWheel(wxMouseEvent &evt) {
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
  m_mouseSelection.Clear();
  RefreshView();
}

void wxTerminalViewCtrl::OnContextMenu(wxContextMenuEvent &evt) {
  wxMenu menu;

  if (m_mouseSelection.HasSelection()) {
    menu.Append(wxID_COPY, _("Copy"));
  }
  menu.Append(wxID_PASTE, _("Paste"));
  menu.AppendSeparator();
  menu.Append(wxID_CLEAR, _("Clear buffer"));

  menu.Bind(wxEVT_MENU, &wxTerminalViewCtrl::OnCopy, this, wxID_COPY);
  menu.Bind(wxEVT_MENU, &wxTerminalViewCtrl::OnPaste, this, wxID_PASTE);
  menu.Bind(wxEVT_MENU, &wxTerminalViewCtrl::OnClearBuffer, this, wxID_CLEAR);

  m_contextMenuShowing = true;
  PopupMenu(&menu);
  m_contextMenuShowing = false;
  evt.Skip();
}

void wxTerminalViewCtrl::OnCopy(wxCommandEvent &evt) {
  wxUnusedVar(evt);
  Copy();
}

void wxTerminalViewCtrl::OnClearBuffer(wxCommandEvent &evt) {
  wxUnusedVar(evt);
  ClearAll();
}

void wxTerminalViewCtrl::OnPaste(wxCommandEvent &evt) {
  wxUnusedVar(evt);
  Paste();
}

void wxTerminalViewCtrl::OnFocus(wxFocusEvent &evt) {
  // Ensure we can receive keyboard events
  evt.Skip();
  m_hasFocusBorder = true;
  SetCursor(wxCursor(wxCURSOR_IBEAM));
  Refresh();
}

void wxTerminalViewCtrl::OnLostFocus(wxFocusEvent &evt) {
  evt.Skip();
  m_hasFocusBorder = false;
  SetCursor(wxCursor(wxCURSOR_ARROW));
  ClearMouseSelection();
  Refresh();
}

void wxTerminalViewCtrl::DrawFocusBorder(wxDC &dc) const {
  if (!m_hasFocusBorder) {
    return;
  }

  wxRect r = GetClientRect();
  if (r.width <= 1 || r.height <= 1) {
    return;
  }

#ifdef __WXMSW__
  constexpr int kFocusPenWidth = 1;
  wxColour focusRectColour("#5E9ED6");
  wxPen pen(focusRectColour, kFocusPenWidth);
  pen.SetCap(wxCAP_BUTT);
  pen.SetJoin(wxJOIN_MITER);
#elif defined(__WXMAC__)
  constexpr int kFocusPenWidth = 2;
  wxColour focusRectColour("#5E9ED6");
  wxPen pen(focusRectColour, kFocusPenWidth);
  pen.SetCap(wxCAP_ROUND);
  pen.SetJoin(wxJOIN_ROUND);
#else
  constexpr int kFocusPenWidth = 1;
  wxColour focusRectColour("#3DAEE9");
  wxPen pen(focusRectColour, kFocusPenWidth);
  pen.SetCap(wxCAP_BUTT);
  pen.SetJoin(wxJOIN_MITER);
#endif

  dc.SetPen(pen);
  dc.SetBrush(*wxTRANSPARENT_BRUSH);
  dc.DrawRectangle(GetClientRect());
}

void wxTerminalViewCtrl::OnCharHook(wxKeyEvent &evt) {
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
    if (m_mouseSelection.HasSelection()) {
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

void wxTerminalViewCtrl::OnKeyDown(wxKeyEvent &evt) {
  if (!HasFocus()) {
    evt.Skip();
    return;
  }

  const int key = evt.GetKeyCode();

  // Clear keyboard selection on any non-shift, non-modified keypress
  if (!evt.ShiftDown() && !evt.ControlDown() && !evt.RawControlDown() &&
      !evt.AltDown() && m_mouseSelection.HasSelection() &&
      key != WXK_SHIFT && key != WXK_RAW_CONTROL && key != WXK_ALT &&
      key != WXK_CONTROL) {
    ClearMouseSelection();
  }

  // Handle Ctrl combinations
  const bool ctrl = evt.RawControlDown();

#ifdef __WXMAC__
  // On macOS, Cmd+C/V for copy/paste
  // (ControlDown() = Cmd key)
  if (evt.ControlDown() && !evt.AltDown()) {
    if (m_mouseSelection.HasSelection() && (key == 'C' || key == 'c')) {
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

  if (evt.GetModifiers() == wxMOD_RAW_CONTROL) {
    // Handle Ctrl+C - Copy if text is selected,
    // otherwise send SIGINT
    if (key == 'C' || key == 'c') {
      TLOG_DEBUG() << "Sending Ctrl-C" << std::endl;
      SendCtrlC();
      return;
    }

    // Handle common Ctrl+<CHAR> combinations
    if (key == 'U' || key == 'u') {
      SendCtrlU();
      return;
    }

    if (key == 'L' || key == 'l') {
      SendCtrlL();
      return;
    }

    if (key == 'D' || key == 'd') {
      SendCtrlD();
      return;
    }

    if (key == 'E' || key == 'e') {
      SendCtrlE();
      return;
    }

    if (key == 'A' || key == 'a') {
      SendCtrlA();
      return;
    }

    if (key == 'W' || key == 'w') {
      SendCtrlW();
      return;
    }

    if (key == 'R' || key == 'r') {
      SendCtrlR();
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
    return;
  }
}

void wxTerminalViewCtrl::ExtendKeyboardSelection(int newCol, int newRow) {
  int rows = static_cast<int>(m_core.Rows());
  int cols = static_cast<int>(m_core.Cols());

  // Clamp column
  newCol = std::clamp(newCol, 0, cols - 1);

  // Auto-scroll and clamp row
  if (newRow < 0) {
    std::size_t vs = m_core.ViewStart();
    if (vs > 0) {
      m_core.SetViewStart(vs - 1);
      // Adjust anchor to stay in sync with the shifted viewport
      m_mouseSelection.anchor.y += 1;
    }
    newRow = 0;
  } else if (newRow >= rows) {
    std::size_t vs = m_core.ViewStart();
    std::size_t maxVs =
        m_core.TotalLines() > static_cast<std::size_t>(rows)
            ? m_core.TotalLines() - static_cast<std::size_t>(rows)
            : 0;
    if (vs < maxVs) {
      m_core.SetViewStart(vs + 1);
      m_mouseSelection.anchor.y -= 1;
    }
    newRow = rows - 1;
  }

  m_mouseSelection.current = {newCol, newRow};
  m_mouseSelection.viewStart = m_core.ViewStart();
  RefreshView();
}

bool wxTerminalViewCtrl::HandleSpecialKeys(wxKeyEvent &key_event) {
  auto key = key_event.GetKeyCode();
  bool shift = key_event.ShiftDown();

  // Shift+navigation: keyboard selection
  if (shift) {
    bool isNav = (key == WXK_UP || key == WXK_DOWN || key == WXK_LEFT ||
                  key == WXK_RIGHT || key == WXK_HOME || key == WXK_END ||
                  key == WXK_PAGEUP || key == WXK_PAGEDOWN);
    if (isNav) {
      // Initialize selection at cursor if not active
      if (!m_mouseSelection.active) {
        auto cursor = m_core.Cursor();
        std::size_t vs = m_core.ViewStart();
        std::size_t ss = m_core.ShellStart();
        int screenRow = static_cast<int>(ss - vs) + cursor.y;
        m_mouseSelection.anchor = {cursor.x, screenRow};
        m_mouseSelection.current = m_mouseSelection.anchor;
        m_mouseSelection.viewStart = vs;
        m_mouseSelection.active = true;
      }

      int col = m_mouseSelection.current.x;
      int row = m_mouseSelection.current.y;
      int cols = static_cast<int>(m_core.Cols());
      int rows = static_cast<int>(m_core.Rows());

      switch (key) {
      case WXK_LEFT:
        ExtendKeyboardSelection(col - 1, row);
        break;
      case WXK_RIGHT:
        ExtendKeyboardSelection(col + 1, row);
        break;
      case WXK_UP:
        ExtendKeyboardSelection(col, row - 1);
        break;
      case WXK_DOWN:
        ExtendKeyboardSelection(col, row + 1);
        break;
      case WXK_HOME:
        ExtendKeyboardSelection(0, row);
        break;
      case WXK_END:
        ExtendKeyboardSelection(cols - 1, row);
        break;
      case WXK_PAGEUP:
        ExtendKeyboardSelection(col, row - rows);
        break;
      case WXK_PAGEDOWN:
        ExtendKeyboardSelection(col, row + rows);
        break;
      }
      return true;
    }
  }

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

const wxFont &wxTerminalViewCtrl::GetCachedFont(bool bold,
                                                bool underlined) const {
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

void wxTerminalViewCtrl::Copy() {
  TLOG_DEBUG() << "Copy is called!" << std::endl;
  if (!m_mouseSelection.HasSelection()) {
    TLOG_DEBUG() << "No selection is active - will "
                    "do nothing"
                 << std::endl;
    return;
  }

  // Get the selected text using linear (reading-order) selection
  wxString selection;
  wxPoint s, e;
  m_mouseSelection.GetNormalized(s, e);
  TLOG_DEBUG() << "Copying content: (" << s.x << "," << s.y << ")-(" << e.x
               << "," << e.y << ")" << std::endl;
  auto viewArea = m_core.GetViewArea();
  int cols = static_cast<int>(m_core.Cols());
  for (int y = s.y; y <= e.y && y < static_cast<int>(viewArea.size()); ++y) {
    const auto &row = *viewArea[y];
    int startCol = (y == s.y) ? s.x : 0;
    int endCol = (y == e.y) ? e.x : cols - 1;
    for (int x = startCol; x <= endCol && x < static_cast<int>(row.size());
         ++x) {
      selection += wxUniChar(row[x].ch);
    }
    // Only insert newline between rows if the next row is NOT a soft-wrap
    // continuation of this row
    if (y != e.y) {
      bool nextIsWrapped = m_core.IsViewRowWrapped(y + 1);
      if (!nextIsWrapped) {
        selection.Trim();
      }
      if (!nextIsWrapped) {
        selection += "\n";
      }
    }
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
  RefreshView();
}

void wxTerminalViewCtrl::Paste() {
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

void wxTerminalViewCtrl::ClearAll() {
  Feed("\033[2J\033[3J");
  m_userSelection.Clear();
  m_mouseSelection.Clear();
  RefreshView();
}

static bool IsCommonShell(const wxString &name) {
  static std::vector<wxString> common_shells{
      "wsl.exe", "ssh.exe", "git-bash.exe",   "bash.exe",
      "zsh.exe", "cmd.exe", "powershell.exe",
  }; // we include ssh here
  for (const auto &shell : common_shells) {
    if (name.Contains(shell)) {
      return true;
    }
  }
  return false;
}

bool wxTerminalViewCtrl::IsShell(const wxString &shell_name,
                                 const wxArrayString &children) const {
  if (children.empty()) {
    return false;
  }
  for (const auto &image_name : children) {
    // Check the first process that is a know shell
    if (!IsCommonShell(image_name)) {
      continue;
    }
    return image_name.Contains(shell_name);
  }
  return false;
}

bool wxTerminalViewCtrl::IsCmdShell() const {
#ifndef __WXMSW__
  return false;
#else
  if (!m_backend) {
    return false;
  }
  bool is_cmd = IsShell("cmd.exe", m_backend->GetChildren());
  if (is_cmd) {
    TLOG_DEBUG() << "Current shell is CMD. Checked the list: "
                 << m_backend->GetChildren() << std::endl;
  }
  return is_cmd;
#endif
}

bool wxTerminalViewCtrl::IsPowerShell() const {
#ifndef __WXMSW__
  return false;
#else
  if (!m_backend) {
    return false;
  }
  bool is_ps = IsShell("powershell.exe", m_backend->GetChildren());
  if (is_ps) {
    TLOG_DEBUG() << "Current shell is PowerShell. Checked the list: "
                 << m_backend->GetChildren() << std::endl;
  }
  return is_ps;
#endif
}

bool wxTerminalViewCtrl::IsUnixKeyboardMode() const {
#ifndef __WXMSW__
  return true;
#else
  if (!m_backend) {
    return false;
  }
  wxArrayString children = m_backend->GetChildren();
  static std::vector<wxString> unix_like_shells{
      "wsl.exe", "ssh.exe", "git-bash.exe", "bash.exe", "zsh.exe",
  };
  for (const auto &unix_shell : unix_like_shells) {
    if (IsShell(unix_shell, children)) {
      return true;
    }
  }
  return false;
#endif
}

void wxTerminalViewCtrl::SetSelectionDelimChars(const wxString &delims) {
  m_selectionDelimChars.clear();
  for (const auto &delim : delims) {
    m_selectionDelimChars.insert(delim);
  }
}

std::optional<wxRect> wxTerminalViewCtrl::SelectionRectFromMousePoint(
    const wxPoint &pt,
    std::function<bool(const wxUniChar &)> is_valid_char) const {
  auto res = PointToCell(pt);
  if (!res.has_value()) {
    return std::nullopt;
  }
  auto cell = std::move(res.value());
  auto row = m_core.ViewBufferRow(cell.y);
  // sanity
  if (row.empty() || cell.x >= static_cast<int>(row.size()) || cell.x < 0) {
    return std::nullopt;
  }

  // Select as much we can
  int selection_start{cell.x};
  int selection_end{cell.x};

  // Go left
  for (int x = cell.x; x >= 0; x--) {
    if (x >= static_cast<int>(row.size())) {
      break;
    }
    wxUniChar ch{row[x].ch};
    if (!is_valid_char(ch)) {
      break;
    }
    selection_start = x;
  }

  // Go right
  for (int x = cell.x + 1; x < static_cast<int>(row.size()); x++) {
    wxUniChar ch{row[x].ch};
    if (!is_valid_char(ch)) {
      break;
    }
    selection_end = x;
  }

  if (selection_end - selection_start < 0) {
    return std::nullopt;
  }
  return wxRect{
      selection_start,
      cell.y,
      selection_end - selection_start + 1,
      1,
  };
}

void wxTerminalViewCtrl::OnMouseLeftDoubleClick(wxMouseEvent &evt) {
  ClearMouseSelection();

  auto is_valid_char = [this](const wxUniChar &ch) -> bool {
    return !m_selectionDelimChars.contains(ch);
  };
  auto res =
      SelectionRectFromMousePoint(evt.GetPosition(), std::move(is_valid_char));
  if (!res.has_value()) {
    return;
  }

  // SelectionRectFromMousePoint returns a wxRect in view cell coords:
  // x=startCol, y=row, width=count, height=1
  wxRect r = res.value();
  m_mouseSelection.anchor = {r.GetLeft(), r.GetTop()};
  m_mouseSelection.current = {r.GetRight(), r.GetBottom()};
  m_mouseSelection.viewStart = m_core.ViewStart();
  m_mouseSelection.active = true;
  RefreshView();
}

void wxTerminalViewCtrl::DoClickable(wxMouseEvent &event, bool fire_event) {
  ClearMouseSelection();
  m_core.ClearClickedRange();

  auto is_valid_char = [this](const wxUniChar &ch) -> bool {
    return !m_selectionDelimChars.contains(ch);
  };

  auto res = SelectionRectFromMousePoint(event.GetPosition(),
                                         std::move(is_valid_char));
  if (!res.has_value()) {
    return;
  }

  // rect is in pixels, convert it to cells.
  wxRect selected_rect = res.value();
  selected_rect.y = m_core.ViewStart() + selected_rect.y;

  // Remember the selected rect and fire an event.
  m_core.SetClickedRange(selected_rect);
  wxString clicked_text = m_core.GetClickedText();
  clicked_text.Trim().Trim(false);

  if (clicked_text.empty()) {
    m_core.ClearClickedRange();
    return;
  }
  SetCursor(wxCURSOR_HAND);

  if (fire_event) {
    wxTerminalEvent click_event{wxEVT_TERMINAL_TEXT_LINK};
    click_event.SetClickedText(clicked_text);
    click_event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(click_event);
  }
}

wxString wxTerminalViewCtrl::GetRange(std::size_t row, std::size_t col,
                                      std::size_t count) {
  return m_core.GetTextRange(row, col, count);
}

void wxTerminalViewCtrl::RefreshView(bool now) {
#if USE_TIMER_REFRESH
  if (now) {
    m_needsRepaint = false;
    Refresh();
    return;
  }
  m_needsRepaint = true;
#else
  wxUnusedVar(now);
  Refresh();
#endif
}
