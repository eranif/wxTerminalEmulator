#include "terminal_core.h"

#include <algorithm>
#include <cstring>

#include <wx/string.h>

#include "libtsm.h"
#include "terminal_logger.h"

namespace terminal {

static ColourSpec MakeAnsiSpec(int index, bool bright = false) {
  ColourSpec s;
  s.kind = ColourSpec::Kind::Ansi;
  s.ansi = ColourIndex{index, bright};
  return s;
}

static ColourSpec MakeTrueColorSpec(std::uint32_t rgb) {
  ColourSpec s;
  s.kind = ColourSpec::Kind::TrueColor;
  s.rgb = rgb;
  return s;
}

// Convert tsm_screen_attr color fields to our ColourSpec
static std::optional<ColourSpec>
ConvertFgColor(const struct tsm_screen_attr *attr) {
  if (attr->fccode == TSM_COLOR_FOREGROUND)
    return std::nullopt; // default foreground
  if (attr->fccode >= 0 && attr->fccode < 16) {
    bool bright = attr->fccode >= 8;
    int idx = bright ? attr->fccode - 8 : attr->fccode;
    return MakeAnsiSpec(idx, bright);
  }
  // fccode == -1: RGB (used for 256-color indices 16-255 and true color)
  return MakeTrueColorSpec((static_cast<uint32_t>(attr->fr) << 16) |
                           (static_cast<uint32_t>(attr->fg) << 8) |
                           static_cast<uint32_t>(attr->fb));
}

static std::optional<ColourSpec>
ConvertBgColor(const struct tsm_screen_attr *attr) {
  if (attr->bccode == TSM_COLOR_BACKGROUND)
    return std::nullopt; // default background
  if (attr->bccode >= 0 && attr->bccode < 16) {
    bool bright = attr->bccode >= 8;
    int idx = bright ? attr->bccode - 8 : attr->bccode;
    return MakeAnsiSpec(idx, bright);
  }
  return MakeTrueColorSpec((static_cast<uint32_t>(attr->br) << 16) |
                           (static_cast<uint32_t>(attr->bg) << 8) |
                           static_cast<uint32_t>(attr->bb));
}

TerminalCore::TerminalCore(std::size_t rows, std::size_t cols,
                           std::size_t maxLines)
    : m_rows(rows), m_cols(cols), m_maxLines(maxLines) {
  tsm_screen_new(&m_tsmScreen, nullptr, nullptr);
  tsm_screen_resize(m_tsmScreen, static_cast<unsigned int>(cols),
                    static_cast<unsigned int>(rows));
  tsm_screen_set_max_sb(m_tsmScreen, static_cast<unsigned int>(maxLines));

  tsm_vte_new(&m_tsmVte, m_tsmScreen, TsmWriteCb, this, nullptr, nullptr);
  tsm_vte_set_osc_cb(m_tsmVte, TsmOscCb, this);
  tsm_vte_set_bell_cb(m_tsmVte, TsmBellCb, this);

  m_activeScreen.assign(m_rows, std::vector<Cell>(m_cols));
  m_activeScreenWrapped.assign(m_rows, false);
  m_viewStart = 0;
}

TerminalCore::~TerminalCore() {
  if (m_tsmVte)
    tsm_vte_unref(m_tsmVte);
  if (m_tsmScreen)
    tsm_screen_unref(m_tsmScreen);
}

void TerminalCore::SetTheme(const wxTerminalTheme &theme) {
  m_theme = theme;
  SyncPalette();
}

void TerminalCore::SyncPalette() {
  uint8_t palette[TSM_COLOR_NUM][3];
  auto setEntry = [&](int idx, const wxColour &c) {
    palette[idx][0] = c.Red();
    palette[idx][1] = c.Green();
    palette[idx][2] = c.Blue();
  };
  setEntry(TSM_COLOR_BLACK, m_theme.black);
  setEntry(TSM_COLOR_RED, m_theme.red);
  setEntry(TSM_COLOR_GREEN, m_theme.green);
  setEntry(TSM_COLOR_YELLOW, m_theme.yellow);
  setEntry(TSM_COLOR_BLUE, m_theme.blue);
  setEntry(TSM_COLOR_MAGENTA, m_theme.magenta);
  setEntry(TSM_COLOR_CYAN, m_theme.cyan);
  setEntry(TSM_COLOR_LIGHT_GREY, m_theme.white);
  setEntry(TSM_COLOR_DARK_GREY, m_theme.brightBlack);
  setEntry(TSM_COLOR_LIGHT_RED, m_theme.brightRed);
  setEntry(TSM_COLOR_LIGHT_GREEN, m_theme.brightGreen);
  setEntry(TSM_COLOR_LIGHT_YELLOW, m_theme.brightYellow);
  setEntry(TSM_COLOR_LIGHT_BLUE, m_theme.brightBlue);
  setEntry(TSM_COLOR_LIGHT_MAGENTA, m_theme.brightMagenta);
  setEntry(TSM_COLOR_LIGHT_CYAN, m_theme.brightCyan);
  setEntry(TSM_COLOR_WHITE, m_theme.brightWhite);
  setEntry(TSM_COLOR_FOREGROUND, m_theme.fg);
  setEntry(TSM_COLOR_BACKGROUND, m_theme.bg);
  // Must set palette name to "custom" so get_palette() uses our storage
  tsm_vte_set_palette(m_tsmVte, "custom");
  tsm_vte_set_custom_palette(m_tsmVte, palette);
}

void TerminalCore::SetMaxLines(std::size_t maxLines) {
  m_maxLines = maxLines;
  tsm_screen_set_max_sb(m_tsmScreen, static_cast<unsigned int>(maxLines));
}

std::size_t TerminalCore::AbsRow(std::size_t viewportRow) const {
  return m_viewStart + viewportRow;
}

Cell *TerminalCore::GetCell(const wxPoint &absCoords) {
  std::size_t row = static_cast<std::size_t>(absCoords.y);
  std::size_t col = static_cast<std::size_t>(absCoords.x);
  std::size_t sbSize = m_scrollback.size();

  if (row < sbSize) {
    auto &line = m_scrollback[row];
    if (col >= line.size())
      return nullptr;
    return &line[col];
  }

  std::size_t screenRow = row - sbSize;
  if (screenRow >= m_rows)
    return nullptr;
  if (col >= m_activeScreen[screenRow].size())
    return nullptr;
  return &m_activeScreen[screenRow][col];
}

std::optional<std::size_t> TerminalCore::ViewPortRow(std::size_t absrow) const {
  if (absrow < m_viewStart || absrow >= m_viewStart + m_rows)
    return std::nullopt;
  return absrow - m_viewStart;
}

wxPoint TerminalCore::Cursor() const {
  return wxPoint(static_cast<int>(tsm_screen_get_cursor_x(m_tsmScreen)),
                 static_cast<int>(tsm_screen_get_cursor_y(m_tsmScreen)));
}

const std::vector<Cell> &TerminalCore::BufferRow(std::size_t absRow) const {
  static const std::vector<Cell> empty;
  std::size_t sbSize = m_scrollback.size();

  if (absRow < sbSize)
    return m_scrollback[absRow];

  std::size_t screenRow = absRow - sbSize;
  if (screenRow < m_activeScreen.size())
    return m_activeScreen[screenRow];
  return empty;
}

const std::vector<Cell> &
TerminalCore::ViewBufferRow(std::size_t viewareaRow) const {
  return BufferRow(m_viewStart + viewareaRow);
}

std::vector<const std::vector<Cell> *> TerminalCore::GetViewArea() const {
  std::vector<const std::vector<Cell> *> view(m_rows);
  for (std::size_t r = 0; r < m_rows; ++r)
    view[r] = &BufferRow(m_viewStart + r);
  return view;
}

bool TerminalCore::IsViewRowWrapped(std::size_t viewRow) const {
  std::size_t abs = m_viewStart + viewRow;
  std::size_t sbSize = m_scrollback.size();

  if (abs < sbSize)
    return m_scrollback.IsWrapped(abs);

  std::size_t screenRow = abs - sbSize;
  if (screenRow < m_activeScreenWrapped.size())
    return m_activeScreenWrapped[screenRow];
  return false;
}

void TerminalCore::SetViewStart(std::size_t vs) {
  std::size_t total = TotalLines();
  std::size_t maxVs = total > m_rows ? total - m_rows : 0;
  m_viewStart = std::min(vs, maxVs);
  m_followingBottom = (m_viewStart >= m_scrollback.size());
}

void TerminalCore::Resize(std::size_t rows, std::size_t cols) {
  std::size_t newRows = std::max<std::size_t>(1, rows);
  std::size_t newCols = std::max<std::size_t>(1, cols);

  if (newRows == m_rows && newCols == m_cols)
    return;

  m_rows = newRows;
  m_cols = newCols;

  tsm_screen_resize(m_tsmScreen, static_cast<unsigned int>(m_cols),
                    static_cast<unsigned int>(m_rows));

  m_activeScreen.assign(m_rows, std::vector<Cell>(m_cols));
  m_activeScreenWrapped.assign(m_rows, false);

  // Resizing may have changed scrollback count
  m_trackedSbCount = tsm_screen_sb_get_line_count(m_tsmScreen);

  RefreshActiveScreen();

  // Clamp viewStart
  std::size_t maxVs = TotalLines() > m_rows ? TotalLines() - m_rows : 0;
  if (m_followingBottom)
    m_viewStart = m_scrollback.size();
  else
    m_viewStart = std::min(m_viewStart, maxVs);
}

void TerminalCore::SetViewportSize(std::size_t rows, std::size_t cols) {
  Resize(rows, cols);
}

void TerminalCore::AppendLine(const std::string &line) {
  std::string data = line + "\n";
  PutData(data);
}

void TerminalCore::ClearScreen() {
  tsm_screen_erase_screen(m_tsmScreen, false);
  tsm_screen_clear_sb(m_tsmScreen);
  m_scrollback.clear();
  m_trackedSbCount = 0;
  m_viewStart = 0;
  m_followingBottom = true;
  RefreshActiveScreen();
}

void TerminalCore::MoveCursor(std::size_t row, std::size_t col) {
  tsm_screen_move_to(m_tsmScreen, static_cast<unsigned int>(col),
                     static_cast<unsigned int>(row));
}

void TerminalCore::Reset() {
  tsm_vte_hard_reset(m_tsmVte);
  m_scrollback.clear();
  m_trackedSbCount = 0;
  m_activeScreen.assign(m_rows, std::vector<Cell>(m_cols));
  m_activeScreenWrapped.assign(m_rows, false);
  m_viewStart = 0;
  m_followingBottom = true;
}

void TerminalCore::PutData(const std::string &data) {
  TLOG_IF_TRACE { TLOG_TRACE() << "PutData len=" << data.size() << std::endl; }

  // Handle ESC[3J (erase scrollback) which libtsm doesn't support
  if (data.find("\033[3J") != std::string::npos) {
    m_scrollback.clear();
    m_trackedSbCount = 0;
    tsm_screen_clear_sb(m_tsmScreen);
    m_viewStart = 0;
    m_followingBottom = true;
  }

  std::size_t prevSb = tsm_screen_sb_get_line_count(m_tsmScreen);
  tsm_vte_input(m_tsmVte, data.c_str(), data.size());
  CaptureScrollback(prevSb);
  RefreshActiveScreen();

  // Auto-follow bottom if user was at bottom
  if (m_followingBottom)
    m_viewStart = m_scrollback.size();
}

void TerminalCore::CaptureScrollback(std::size_t prevSbCount) {
  std::size_t newSbCount = tsm_screen_sb_get_line_count(m_tsmScreen);
  if (newSbCount <= prevSbCount)
    return;

  std::size_t delta = newSbCount - prevSbCount;

  // The top `delta` rows of the previous active screen have scrolled off.
  // Push them into our scrollback.
  for (std::size_t i = 0; i < delta && i < m_activeScreen.size(); ++i) {
    bool wrapped =
        (i < m_activeScreenWrapped.size()) && m_activeScreenWrapped[i];
    m_scrollback.push_back(std::move(m_activeScreen[i]), wrapped);
  }

  // Trim scrollback to max
  while (m_scrollback.size() > m_maxLines) {
    m_scrollback.pop_front();
  }

  m_trackedSbCount = newSbCount;
}

void TerminalCore::RefreshActiveScreen() {
  // Reset active screen to proper dimensions (rows may have been moved during
  // scrollback capture)
  m_activeScreen.assign(m_rows, std::vector<Cell>(m_cols));

  tsm_screen_draw(m_tsmScreen, TsmDrawCb, this);

  // Compute soft-wrap heuristic
  for (std::size_t r = 0; r + 1 < m_rows; ++r) {
    const auto &row = m_activeScreen[r];
    m_activeScreenWrapped[r] = !row.empty() && row.back().ch != U' ';
  }
  if (m_rows > 0)
    m_activeScreenWrapped[m_rows - 1] = false;
}

// --- libtsm callbacks ---

void TerminalCore::TsmWriteCb(struct tsm_vte * /*vte*/, const char *u8,
                              size_t len, void *data) {
  auto *self = static_cast<TerminalCore *>(data);
  if (self->m_responseCallback)
    self->m_responseCallback(std::string(u8, len));
}

void TerminalCore::TsmOscCb(struct tsm_vte * /*vte*/, const char *u8,
                            size_t len, void *data) {
  auto *self = static_cast<TerminalCore *>(data);
  if (!self->m_titleCallback || len < 3)
    return;

  std::string osc(u8, len);
  // OSC 0;title or OSC 2;title
  if ((osc[0] == '0' || osc[0] == '2') && osc[1] == ';') {
    self->m_titleCallback(osc.substr(2));
  }
}

void TerminalCore::TsmBellCb(struct tsm_vte * /*vte*/, void *data) {
  auto *self = static_cast<TerminalCore *>(data);
  if (self->m_bellCallback)
    self->m_bellCallback();
}

int TerminalCore::TsmDrawCb(struct tsm_screen * /*con*/, uint64_t /*id*/,
                            const uint32_t *ch, size_t len,
                            unsigned int /*width*/, unsigned int posx,
                            unsigned int posy,
                            const struct tsm_screen_attr *attr,
                            tsm_age_t /*age*/, void *data) {
  auto *self = static_cast<TerminalCore *>(data);

  if (posy >= self->m_rows || posx >= self->m_cols)
    return 0;

  Cell cell;
  cell.ch = (len > 0) ? static_cast<char32_t>(ch[0]) : U' ';

  // Convert attributes
  if (attr->bold)
    cell.SetBold(true);
  if (attr->underline)
    cell.SetUnderlined(true);
  if (attr->inverse)
    cell.SetReverse(true);

  // Convert colors
  auto fgSpec = ConvertFgColor(attr);
  auto bgSpec = ConvertBgColor(attr);
  if (fgSpec.has_value() || bgSpec.has_value()) {
    CellColours colours;
    colours.fg = fgSpec;
    colours.bg = bgSpec;
    cell.colours = colours;
  }

  self->m_activeScreen[posy][posx] = cell;
  return 0;
}

// --- Selection / Clicked Range ---

wxString TerminalCore::Flatten() const {
  wxString out;
  out.reserve(m_rows * m_cols * 2);
  for (std::size_t r = 0; r < m_rows; ++r) {
    const auto &row = BufferRow(m_viewStart + r);
    for (const auto &cell : row) {
      out << wxString(wxUniChar(cell.ch));
    }
    out << "\n";
  }
  return out;
}

void TerminalCore::DoSetClickedRange(bool b) {
  if (m_clickedRect.IsEmpty())
    return;

  for (int row = m_clickedRect.y; row < m_clickedRect.height + m_clickedRect.y;
       ++row) {
    for (int col = m_clickedRect.x; col < m_clickedRect.width + m_clickedRect.x;
         ++col) {
      auto cell = GetCell(wxPoint{col, row});
      if (cell)
        cell->SetClicked(b);
    }
  }
}

void TerminalCore::SetClickedRange(const wxRect &absRect) {
  DoSetClickedRange(false);
  m_clickedRect = absRect;
  DoSetClickedRange(true);
}

bool TerminalCore::ClearClickedRange() {
  if (m_clickedRect.IsEmpty())
    return false;
  SetClickedRange({});
  return true;
}

wxString TerminalCore::GetClickedText() const {
  if (m_clickedRect.IsEmpty())
    return wxEmptyString;
  TLOG_DEBUG() << "Clicked rect:" << m_clickedRect << std::endl;

  wxString clicked_text =
      GetTextRange(m_clickedRect.y, m_clickedRect.x, m_clickedRect.width);
  TLOG_DEBUG() << "Clicked text:" << clicked_text << std::endl;
  return clicked_text;
}

wxString TerminalCore::GetTextRange(std::size_t row, std::size_t col,
                                    std::size_t count) const {
  const auto &line = BufferRow(row);
  if (col >= line.size() || count == 0)
    return wxEmptyString;

  wxString text_range;
  text_range.reserve(count);
  for (std::size_t i = col; i < line.size() && count > 0; ++i) {
    text_range << wxString{wxUniChar{line[i].ch}};
    count--;
  }
  return text_range;
}

} // namespace terminal
