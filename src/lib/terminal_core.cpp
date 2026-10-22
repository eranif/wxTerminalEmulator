#include "terminal_core.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>

#include <wx/base64.h>
#include <wx/buffer.h>
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
  // libtsm calls this for every change of the mouse settings. Only report when
  // the program starts or stops tracking.
  tsm_vte_set_mouse_cb(
      m_tsmVte,
      [](struct tsm_vte *, enum tsm_mouse_track_mode mode, bool, void *data) {
        auto *self = static_cast<TerminalCore *>(data);
        const bool tracking = mode != TSM_MOUSE_TRACK_DISABLE;
        if (tracking == self->m_mouseTracking) {
          return;
        }
        self->m_mouseTracking = tracking;
        if (self->m_mouseTrackingCallback)
          self->m_mouseTrackingCallback(tracking);
      },
      this);

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

bool TerminalCore::IsCursorKeyMode() const {
  unsigned int flags = tsm_vte_get_flags(m_tsmVte);
  return (flags & TSM_VTE_FLAG_CURSOR_KEY_MODE) != 0;
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
  uint64_t evictedBefore = tsm_screen_sb_get_evict_count(m_tsmScreen);
  tsm_vte_input(m_tsmVte, data.c_str(), data.size());
  unsigned int flagsAfter = tsm_screen_get_flags(m_tsmScreen);

  if ((flagsBefore ^ flagsAfter) & TSM_SCREEN_ALTERNATE) {
    bool entered = (flagsAfter & TSM_SCREEN_ALTERNATE) != 0;
    if (m_altScreenCallback)
      m_altScreenCallback(entered);
  }

  RefreshActiveScreen();

  if (m_followingBottom) {
    // Auto-follow bottom if user was at bottom
    m_viewStart = ShellStart();
  } else {
    // User scrolled back: keep the viewport pinned to the same content.
    // When the scrollback is full, each new line evicts one from the top
    // and shifts all absolute indexes down; compensate so the visible
    // content does not drift while output streams in.
    uint64_t evicted =
        tsm_screen_sb_get_evict_count(m_tsmScreen) - evictedBefore;
    m_viewStart = (m_viewStart > evicted)
                      ? m_viewStart - static_cast<std::size_t>(evicted)
                      : 0;
  }

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

static int HexDigitValue(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

// Turn the URI of an OSC 7 sequence ("file://host/percent%20encoded/path") into
// a plain path. Returns an empty string when the URI is not usable.
static std::string DecodeOsc7Uri(const std::string &uri) {
  static const std::string kScheme = "file://";
  if (uri.compare(0, kScheme.size(), kScheme) != 0)
    return {};

  // Skip the host: the path starts at the first '/' after "file://".
  const std::size_t pathStart = uri.find('/', kScheme.size());
  if (pathStart == std::string::npos)
    return {};

  std::string path;
  path.reserve(uri.size() - pathStart);
  for (std::size_t i = pathStart; i < uri.size(); ++i) {
    if (uri[i] == '%' && i + 2 < uri.size() && HexDigitValue(uri[i + 1]) >= 0 &&
        HexDigitValue(uri[i + 2]) >= 0) {
      path += static_cast<char>(HexDigitValue(uri[i + 1]) * 16 +
                                HexDigitValue(uri[i + 2]));
      i += 2;
    } else {
      path += uri[i];
    }
  }

#ifdef _WIN32
  // "/C:/dir" -> "C:/dir"
  if (path.size() >= 3 && path[0] == '/' &&
      std::isalpha(static_cast<unsigned char>(path[1])) && path[2] == ':' &&
      (path.size() == 3 || path[3] == '/')) {
    path.erase(0, 1);
  }
#endif
  return path;
}

// --- OSC 7501: Program Status Protocol (revision 0.4) ---
// OSC 7501 ; key=value : key=value ... ST

static bool IsStatusNameChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' ||
         c == '+' || c == '-';
}

// 1-32 characters: the "app" value and one segment of an "id".
static bool IsStatusName(const std::string &s) {
  return !s.empty() && s.size() <= 32 &&
         std::all_of(s.begin(), s.end(), IsStatusNameChar);
}

// Slash separated path: up to 8 segments, up to 128 bytes.
static bool IsStatusRecordId(const std::string &id) {
  if (id.size() > 128)
    return false;
  int levels = 0;
  std::size_t start = 0;
  while (true) {
    const std::size_t slash = id.find('/', start);
    const std::size_t len =
        slash == std::string::npos ? std::string::npos : slash - start;
    if (!IsStatusName(id.substr(start, len)) || ++levels > 8)
      return false;
    if (slash == std::string::npos)
      return true;
    start = slash + 1;
  }
}

static bool IsStatusValueChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' ||
         c == ',' || c == '+' || c == '/' || c == '=' || c == '-';
}

// Decode a base64 "title" or "msg" value (padding is optional). Fails for text
// that is too long, is not base64 or UTF-8, or has control characters.
static bool DecodeStatusText(std::string b64, std::size_t maxEncoded,
                             std::size_t maxDecoded, wxString &out) {
  if (b64.size() > maxEncoded)
    return false;
  while (b64.size() % 4 != 0)
    b64 += '=';
  const wxMemoryBuffer decoded = wxBase64Decode(b64.data(), b64.size());
  if (decoded.GetDataLen() == 0 && !b64.empty())
    return false;
  if (decoded.GetDataLen() > maxDecoded)
    return false;
  const wxString text = wxString::FromUTF8(
      static_cast<const char *>(decoded.GetData()), decoded.GetDataLen());
  if (text.empty() && decoded.GetDataLen() != 0)
    return false;
  for (const wxUniChar ch : text) {
    const auto v = static_cast<unsigned int>(ch.GetValue());
    if (v < 0x20 || (v >= 0x7F && v <= 0x9F))
      return false;
  }
  out = text;
  return true;
}

// Parse the part of the sequence after "7501;". Returns false when the whole
// report must be dropped.
static bool ParseProgramStatus(const std::string &body,
                               wxTerminalProgramStatus &out) {
  std::map<std::string, std::string> pairs; // a repeated key: the last wins
  std::size_t start = 0;
  while (start <= body.size()) {
    std::size_t colon = body.find(':', start);
    if (colon == std::string::npos)
      colon = body.size();
    const std::string pair = body.substr(start, colon - start);
    start = colon + 1;

    const std::size_t eq = pair.find('=');
    if (eq == std::string::npos || eq == 0 || eq == pair.size() - 1)
      continue;
    const std::string key = pair.substr(0, eq);
    const std::string value = pair.substr(eq + 1);
    const bool keyOk =
        key.size() <= 16 && std::all_of(key.begin(), key.end(), [](char c) {
          return c >= 'a' && c <= 'z';
        });
    if (keyOk && std::all_of(value.begin(), value.end(), IsStatusValueChar))
      pairs[key] = value;
  }

  static const std::map<std::string, wxTerminalProgramState> kStates = {
      {"idle", wxTerminalProgramState::Idle},
      {"working", wxTerminalProgramState::Working},
      {"done", wxTerminalProgramState::Done},
      {"blocked", wxTerminalProgramState::Blocked},
      {"error", wxTerminalProgramState::Error},
      {"clear", wxTerminalProgramState::Clear},
  };
  const auto stateIt = pairs.find("state");
  if (stateIt == pairs.end())
    return false;
  const auto known = kStates.find(stateIt->second);
  if (known == kStates.end())
    return false;

  wxTerminalProgramStatus status;
  status.state = known->second;

  if (const auto it = pairs.find("id"); it != pairs.end()) {
    if (!IsStatusRecordId(it->second))
      return false;
    status.id = wxString::FromUTF8(it->second);
  }

  if (status.state == wxTerminalProgramState::Blocked) {
    if (const auto it = pairs.find("kind"); it != pairs.end()) {
      if (it->second == "permission")
        status.kind = wxTerminalBlockedKind::Permission;
      else if (it->second == "question")
        status.kind = wxTerminalBlockedKind::Question;
      else if (it->second == "auth")
        status.kind = wxTerminalBlockedKind::Auth;
    }
  }

  if (status.state == wxTerminalProgramState::Working ||
      status.state == wxTerminalProgramState::Blocked) {
    if (const auto it = pairs.find("progress"); it != pairs.end()) {
      const std::string &p = it->second;
      if (p.size() <= 3 && std::all_of(p.begin(), p.end(), [](char c) {
            return c >= '0' && c <= '9';
          })) {
        const int value = std::atoi(p.c_str());
        if (value <= 100)
          status.progress = value;
      }
    }
  }

  if (const auto it = pairs.find("app");
      it != pairs.end() && IsStatusName(it->second))
    status.app = wxString::FromUTF8(it->second);

  if (const auto it = pairs.find("title"); it != pairs.end()) {
    if (!DecodeStatusText(it->second, 256, 192, status.title))
      return false;
  }
  if (const auto it = pairs.find("msg"); it != pairs.end()) {
    if (!DecodeStatusText(it->second, 2732, 2048, status.message))
      return false;
  }

  out = std::move(status);
  return true;
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
    return;
  }

  // OSC 7;file://host/path — current working directory
  if (osc[0] == '7' && osc[1] == ';') {
    std::string path = DecodeOsc7Uri(osc.substr(2));
    if (!path.empty() && self->m_cwdCallback)
      self->m_cwdCallback(path);
    return;
  }

  // OSC 7501;? — does the terminal support the Program Status Protocol?
  if (osc == "7501;?" || osc.rfind("7501;?:", 0) == 0) {
    if (self->m_responseCallback)
      self->m_responseCallback("\x1b]7501;?:states=idle,working,done,blocked,"
                               "error:kinds=permission,question,auth\x1b\\");
    return;
  }

  // OSC 7501;state=...:progress=... — program status (Kiro CLI and others).
  if (osc.rfind("7501;", 0) == 0) {
    wxTerminalProgramStatus status;
    if (self->m_programStatusCallback &&
        ParseProgramStatus(osc.substr(5), status))
      self->m_programStatusCallback(status);
    return;
  }

  // OSC 9;4;<state>;<value> — progress bar (ConEmu, Windows Terminal, iTerm2).
  if (osc.rfind("9;4", 0) == 0 && (osc.size() == 3 || osc[3] == ';')) {
    if (!self->m_progressCallback)
      return;
    int state = 0;
    int value = 0;
    const char *p = osc.c_str() + (osc.size() > 4 ? 4 : osc.size());
    char *end = nullptr;
    state = static_cast<int>(std::strtol(p, &end, 10));
    if (end && *end == ';')
      value = static_cast<int>(std::strtol(end + 1, nullptr, 10));
    if (state < 0 || state > 4)
      return;
    self->m_progressCallback(static_cast<wxTerminalProgressState>(state),
                             std::clamp(value, 0, 100));
    return;
  }

  // OSC 9;9;"C:\dir" — current working directory (ConEmu, Windows Terminal).
  // The path is plain text, optionally in double quotes.
  if (osc.rfind("9;9;", 0) == 0) {
    std::string path = osc.substr(4);
    if (path.size() >= 2 && path.front() == '"' && path.back() == '"')
      path = path.substr(1, path.size() - 2);
    if (!path.empty() && self->m_cwdCallback)
      self->m_cwdCallback(path);
    return;
  }

  // OSC 52;<targets>;<base64> — the program asks to set the clipboard. Only
  // writes are supported: a query ("?") would let a program read the
  // clipboard. libtsm drops OSC bytes past OSC_MAX_LEN (4 KB), so a larger
  // copy (over about 3 KB of text) arrives cut; the view ignores text that is
  // no longer valid UTF-8.
  if (osc.rfind("52;", 0) == 0) {
    const auto dataPos = osc.find(';', 3);
    if (dataPos == std::string::npos || !self->m_clipboardCallback)
      return;
    const std::string payload = osc.substr(dataPos + 1);
    if (payload.empty() || payload == "?")
      return;
    const wxMemoryBuffer decoded = wxBase64Decode(
        payload.data(), payload.size(), wxBase64DecodeMode_SkipWS);
    if (decoded.GetDataLen() == 0)
      return;
    self->m_clipboardCallback(std::string(
        static_cast<const char *>(decoded.GetData()), decoded.GetDataLen()));
  }
}

void TerminalCore::TsmBellCb(struct tsm_vte * /*vte*/, void *data) {
  auto *self = static_cast<TerminalCore *>(data);
  if (self->m_bellCallback)
    self->m_bellCallback();
}

int TerminalCore::TsmDrawCb(struct tsm_screen * /*con*/, uint64_t /*id*/,
                            const uint32_t *ch, size_t len, unsigned int width,
                            unsigned int posx, unsigned int posy,
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

wxString TerminalCore::Flatten(std::size_t from, std::size_t count) const {
  const std::size_t total = TotalLines();
  if (from >= total || count == 0) {
    return wxString();
  }
  // Overflow-safe: count may be std::numeric_limits<size_t>::max().
  const std::size_t end = count > total - from ? total : from + count;

  wxArrayString lines;
  lines.reserve(end - from);
  for (std::size_t r = from; r < end; ++r) {
    wxString line = GetBufferRowCopyString(r);
    line.Trim();
    lines.push_back(line);
  }

  // Trim trailing empty lines, only when reaching the end of the buffer (the
  // unused rows below the shell output); an explicit range keeps its blanks.
  if (end == total) {
    while (!lines.empty()) {
      if (!lines.back().empty()) {
        break;
      }
      lines.pop_back();
    }
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
                                    unsigned char flags, unsigned int pixel_x,
                                    unsigned int pixel_y) {
  return tsm_vte_handle_mouse(m_tsmVte, cell_x, cell_y, pixel_x, pixel_y,
                              button, event, flags);
}

unsigned int TerminalCore::GetMouseTrackingMode() const {
  return tsm_vte_get_mouse_mode(m_tsmVte);
}

} // namespace terminal
