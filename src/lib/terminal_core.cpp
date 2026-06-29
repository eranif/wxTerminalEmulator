#include "terminal_core.h"

#include <algorithm>
#include <cstring>

#include <wx/string.h>

#include "libtsm.h"
#include "terminal_logger.h"
#include "wx/arrstr.h"

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

static std::optional<ColourSpec>
ConvertFgColor(const struct tsm_screen_attr *attr) {
  if (attr->fccode == TSM_COLOR_FOREGROUND)
    return std::nullopt;
  if (attr->fccode >= 0 && attr->fccode < 16) {
    bool bright = attr->fccode >= 8;
    int idx = bright ? attr->fccode - 8 : attr->fccode;
    return MakeAnsiSpec(idx, bright);
  }
  return MakeTrueColorSpec((static_cast<uint32_t>(attr->fr) << 16) |
                           (static_cast<uint32_t>(attr->fg) << 8) |
                           static_cast<uint32_t>(attr->fb));
}

static std::optional<ColourSpec>
ConvertBgColor(const struct tsm_screen_attr *attr) {
  if (attr->bccode == TSM_COLOR_BACKGROUND)
    return std::nullopt;
  if (attr->bccode >= 0 && attr->bccode < 16) {
    bool bright = attr->bccode >= 8;
    int idx = bright ? attr->bccode - 8 : attr->bccode;
    return MakeAnsiSpec(idx, bright);
  }
  return MakeTrueColorSpec((static_cast<uint32_t>(attr->br) << 16) |
                           (static_cast<uint32_t>(attr->bg) << 8) |
                           static_cast<uint32_t>(attr->bb));
}

static Cell ConvertTsmCell(const struct tsm_screen_cell &tsmCell) {
  Cell cell;
  cell.ch = (tsmCell.ch != 0) ? static_cast<char32_t>(tsmCell.ch) : U' ';
  cell.width = tsmCell.width;

  if (tsmCell.attr.bold)
    cell.SetBold(true);
  if (tsmCell.attr.underline)
    cell.SetUnderlined(true);
  if (tsmCell.attr.inverse)
    cell.SetReverse(true);

  auto fgSpec = ConvertFgColor(&tsmCell.attr);
  auto bgSpec = ConvertBgColor(&tsmCell.attr);
  if (fgSpec.has_value() || bgSpec.has_value()) {
    CellColours colours;
    colours.fg = fgSpec;
    colours.bg = bgSpec;
    cell.colours = colours;
  }
  return cell;
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

  // Align libtsm's cursor-cell inversion with the requested caret style.
  // A block caret inverts the glyph beneath it (libtsm's default behavior);
  // a beam/line caret is rendered by the front-end and must NOT invert the
  // underlying glyph, so set TSM_SCREEN_BEAM_CURSOR in that case.
  if (m_theme.isBlockCursor)
    tsm_screen_reset_flags(m_tsmScreen, TSM_SCREEN_BEAM_CURSOR);
  else
    tsm_screen_set_flags(m_tsmScreen, TSM_SCREEN_BEAM_CURSOR);
  RefreshActiveScreen();
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
  tsm_vte_set_palette(m_tsmVte, "custom");
  tsm_vte_set_custom_palette(m_tsmVte, palette);
}

void TerminalCore::SetMaxLines(std::size_t maxLines) {
  m_maxLines = maxLines;
  tsm_screen_set_max_sb(m_tsmScreen, static_cast<unsigned int>(maxLines));
}

bool TerminalCore::HasScroll() const {
  return tsm_screen_sb_get_line_count(m_tsmScreen);
}

std::size_t TerminalCore::ShellStart() const {
  return tsm_screen_sb_get_line_count(m_tsmScreen);
}

std::size_t TerminalCore::TotalLines() const { return ShellStart() + m_rows; }

std::size_t TerminalCore::AbsRow(std::size_t viewportRow) const {
  return m_viewStart + viewportRow;
}

Cell *TerminalCore::GetCell(const wxPoint &absCoords) {
  std::size_t row = static_cast<std::size_t>(absCoords.y);
  std::size_t col = static_cast<std::size_t>(absCoords.x);
  std::size_t sbSize = ShellStart();

  if (row < sbSize) {
    // Check if it's in the scrollback cache
    if (row >= m_sbCacheStart && row < m_sbCacheStart + m_sbCache.size()) {
      auto &line = m_sbCache[row - m_sbCacheStart];
      if (col >= line.size())
        return nullptr;
      return &line[col];
    }
    return nullptr;
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

bool TerminalCore::IsCursorVisible() const {
  unsigned int flags = tsm_vte_get_flags(m_tsmVte);
  return (flags & TSM_VTE_FLAG_TEXT_CURSOR_MODE) != 0;
}

const std::vector<Cell> &TerminalCore::BufferRow(std::size_t absRow) const {
  static const std::vector<Cell> empty;
  std::size_t sbSize = ShellStart();

  if (absRow < sbSize) {
    if (absRow >= m_sbCacheStart &&
        absRow < m_sbCacheStart + m_sbCache.size()) {
      return m_sbCache[absRow - m_sbCacheStart];
    }
    return empty;
  }

  std::size_t screenRow = absRow - sbSize;
  if (screenRow < m_activeScreen.size())
    return m_activeScreen[screenRow];
  return empty;
}

const std::vector<Cell> &
TerminalCore::ViewBufferRow(std::size_t viewareaRow) const {
  return BufferRow(m_viewStart + viewareaRow);
}

std::vector<Cell> TerminalCore::GetBufferRowCopy(std::size_t absRow) const {
  std::size_t sbSize = ShellStart();
  if (absRow < sbSize) {
    // Fast path: row is inside the cached (visible) scrollback window.
    if (absRow >= m_sbCacheStart &&
        absRow < m_sbCacheStart + m_sbCache.size()) {
      return m_sbCache[absRow - m_sbCacheStart];
    }
    // Otherwise read the scrollback line directly from libtsm so callers can
    // access rows that have scrolled out of the viewport.
    return ConvertScrollbackLine(static_cast<unsigned int>(absRow));
  }

  std::size_t screenRow = absRow - sbSize;
  if (screenRow < m_activeScreen.size())
    return m_activeScreen[screenRow];
  return {};
}

wxString TerminalCore::GetBufferRowCopyString(std::size_t absRow) const {
  auto copy = GetBufferRowCopy(absRow);
  wxString out;
  out.reserve(copy.size());
  for (const auto &cell : copy) {
    out << wxString(wxUniChar(cell.ch));
  }
  return out;
}

std::vector<const std::vector<Cell> *> TerminalCore::GetViewArea() const {
  std::vector<const std::vector<Cell> *> view(m_rows);
  for (std::size_t r = 0; r < m_rows; ++r)
    view[r] = &BufferRow(m_viewStart + r);
  return view;
}

void TerminalCore::SetViewStart(std::size_t vs) {
  std::size_t total = TotalLines();
  std::size_t maxVs = total > m_rows ? total - m_rows : 0;
  m_viewStart = std::min(vs, maxVs);
  m_followingBottom = (m_viewStart >= ShellStart());
  RefreshScrollbackCache();
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
  RefreshActiveScreen();

  // Clamp viewStart
  std::size_t maxVs = TotalLines() > m_rows ? TotalLines() - m_rows : 0;
  if (m_followingBottom)
    m_viewStart = ShellStart();
  else
    m_viewStart = std::min(m_viewStart, maxVs);

  RefreshScrollbackCache();
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
  m_sbCache.clear();
  m_sbCacheStart = 0;
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
  m_activeScreen.assign(m_rows, std::vector<Cell>(m_cols));
  m_sbCache.clear();
  m_sbCacheStart = 0;
  m_viewStart = 0;
  m_followingBottom = true;
}

void TerminalCore::PutData(const std::string &data) {
  TLOG_IF_TRACE { TLOG_TRACE() << "PutData len=" << data.size() << std::endl; }

  // Handle ESC[3J (erase scrollback) which libtsm doesn't support
  if (data.find("\033[3J") != std::string::npos) {
    tsm_screen_clear_sb(m_tsmScreen);
    m_sbCache.clear();
    m_sbCacheStart = 0;
    m_viewStart = 0;
    m_followingBottom = true;
  }

  unsigned int flagsBefore = tsm_screen_get_flags(m_tsmScreen);
  tsm_vte_input(m_tsmVte, data.c_str(), data.size());
  unsigned int flagsAfter = tsm_screen_get_flags(m_tsmScreen);

  if ((flagsBefore ^ flagsAfter) & TSM_SCREEN_ALTERNATE) {
    bool entered = (flagsAfter & TSM_SCREEN_ALTERNATE) != 0;
    if (m_altScreenCallback)
      m_altScreenCallback(entered);
  }

  RefreshActiveScreen();

  // Auto-follow bottom if user was at bottom
  if (m_followingBottom)
    m_viewStart = ShellStart();

  RefreshScrollbackCache();
}

void TerminalCore::RefreshActiveScreen() {
  LogFunction log{"TerminalCore::RefreshActiveScreen",
                  TerminalLogLevel::kDebug};
  m_activeScreen.assign(m_rows, std::vector<Cell>(m_cols));
  tsm_screen_draw(m_tsmScreen, TsmDrawCb, this);
}

void TerminalCore::RefreshScrollbackCache() {
  LogFunction log{"TerminalCore::RefreshScrollbackCache",
                  TerminalLogLevel::kDebug};
  std::size_t sbSize = ShellStart();
  if (m_viewStart >= sbSize) {
    m_sbCache.clear();
    m_sbCacheStart = 0;
    return;
  }

  // How many scrollback rows are visible in the viewport?
  std::size_t sbEnd = std::min(m_viewStart + m_rows, sbSize);
  std::size_t count = sbEnd - m_viewStart;

  m_sbCacheStart = m_viewStart;
  m_sbCache.resize(count);

  // Bulk-read from libtsm
  std::vector<struct tsm_screen_cell> buf(count * m_cols);
  std::vector<unsigned int> lens(count);

  int rc = tsm_screen_sb_get_lines(
      m_tsmScreen, static_cast<unsigned int>(m_viewStart),
      static_cast<unsigned int>(count), buf.data(), lens.data(),
      static_cast<unsigned int>(m_cols));

  if (rc != 0) {
    m_sbCache.clear();
    m_sbCacheStart = 0;
    return;
  }

  for (std::size_t r = 0; r < count; ++r) {
    unsigned int lineLen = lens[r];
    m_sbCache[r].resize(lineLen);
    for (unsigned int c = 0; c < lineLen; ++c) {
      m_sbCache[r][c] = ConvertTsmCell(buf[r * m_cols + c]);
    }
  }
}

std::vector<Cell>
TerminalCore::ConvertScrollbackLine(unsigned int lineIdx) const {
  std::vector<struct tsm_screen_cell> buf(m_cols);
  unsigned int len = 0;

  int rc = tsm_screen_sb_get_line_cells(m_tsmScreen, lineIdx, buf.data(), &len,
                                        static_cast<unsigned int>(m_cols));
  if (rc != 0)
    return {};

  std::vector<Cell> row(len);
  for (unsigned int c = 0; c < len; ++c) {
    row[c] = ConvertTsmCell(buf[c]);
  }
  return row;
}

// --- libtsm callbacks ---

void TerminalCore::TsmWriteCb(struct tsm_vte * /*vte*/, const char *u8,
                              size_t len, void *data) {
  auto *self = static_cast<TerminalCore *>(data);
  if (self->m_responseCallback)
    self->m_responseCallback(std::string(u8, len));
}

static std::string OscRgbResponse(const wxColour &c, int oscId) {
  unsigned int r = c.Red() * 257u;
  unsigned int g = c.Green() * 257u;
  unsigned int b = c.Blue() * 257u;
  char buf[64];
  std::snprintf(buf, sizeof(buf), "\x1b]%d;rgb:%04x/%04x/%04x\x1b\\", oscId, r,
                g, b);
  return buf;
}

void TerminalCore::TsmOscCb(struct tsm_vte * /*vte*/, const char *u8,
                            size_t len, void *data) {
  auto *self = static_cast<TerminalCore *>(data);
  if (len < 2)
    return;

  std::string osc(u8, len);

  // OSC 10;? — query foreground color
  if (osc.rfind("10;", 0) == 0 && osc.find('?') != std::string::npos) {
    if (self->m_responseCallback)
      self->m_responseCallback(OscRgbResponse(self->m_theme.fg, 10));
    return;
  }

  // OSC 11;? — query background color
  if (osc.rfind("11;", 0) == 0 && osc.find('?') != std::string::npos) {
    if (self->m_responseCallback)
      self->m_responseCallback(OscRgbResponse(self->m_theme.bg, 11));
    return;
  }

  // OSC 0;title or OSC 2;title
  if ((osc[0] == '0' || osc[0] == '2') && osc[1] == ';') {
    if (self->m_titleCallback)
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
                            unsigned int width, unsigned int posx,
                            unsigned int posy,
                            const struct tsm_screen_attr *attr,
                            tsm_age_t /*age*/, void *data) {
  auto *self = static_cast<TerminalCore *>(data);

  if (posy >= self->m_rows || posx >= self->m_cols)
    return 0;

  Cell cell;
  cell.ch = (len > 0) ? static_cast<char32_t>(ch[0]) : U' ';
  cell.width = width;

  if (attr->bold)
    cell.SetBold(true);
  if (attr->underline)
    cell.SetUnderlined(true);
  if (attr->inverse)
    cell.SetReverse(true);

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
  wxArrayString lines;
  lines.reserve(m_rows);
  for (std::size_t r = 0; r < TotalLines(); ++r) {
    wxString line = GetBufferRowCopyString(r);
    line.Trim();
    lines.push_back(line);
  }

  // Trim trailing empty lines
  while (!lines.empty()) {
    if (!lines.back().empty()) {
      break;
    }
    lines.pop_back();
  }
  return wxJoin(lines, '\n');
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

bool TerminalCore::HandleMouseEvent(unsigned int cell_x, unsigned int cell_y,
                                    unsigned int button, unsigned int event,
                                    unsigned char flags) {
  return tsm_vte_handle_mouse(m_tsmVte, cell_x, cell_y, 0, 0, button, event,
                              flags);
}

unsigned int TerminalCore::GetMouseTrackingMode() const {
  return tsm_vte_get_mouse_mode(m_tsmVte);
}

} // namespace terminal
