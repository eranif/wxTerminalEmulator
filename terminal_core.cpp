#include "terminal_core.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include <wx/string.h>

#include "terminal_logger.h"

namespace terminal {

static std::string OscRgbResponse(const wxColour &c, int oscId) {
  const auto to16 = [](std::uint32_t c) {
    return static_cast<unsigned int>((c & 0xFFu) * 257u);
  };
  const unsigned int r = to16(c.Red());
  const unsigned int g = to16(c.Green());
  const unsigned int b = to16(c.Blue());

  std::ostringstream oss;
  oss << "\x1b]";
  oss << oscId << ";rgb:" << std::hex << std::setfill('0') << std::setw(4) << r
      << "/" << std::setw(4) << g << "/" << std::setw(4) << b << "\x1b\\";
  return oss.str();
}

TerminalCore::TerminalCore(std::size_t rows, std::size_t cols,
                           std::size_t maxLines)
    : m_rows(rows), m_cols(cols), m_maxLines(maxLines) {
  m_attr.SetColours(m_theme);
  Reset();
}

void TerminalCore::SetTheme(const wxTerminalTheme &theme) {
  m_theme = theme;
  m_attr.SetColours(m_theme);
}

static ColourIndex MakeColourIndex(int index, bool bright = false) {
  return ColourIndex{index, bright};
}

static void ResetColours(Cell &cell) { cell.colours = CellColours{}; }

static ColourSpec MakeAnsiSpec(int index, bool bright = false) {
  ColourSpec s;
  s.kind = ColourSpec::Kind::Ansi;
  s.ansi = ColourIndex{index, bright};
  return s;
}

static ColourSpec MakePaletteSpec(int index) {
  ColourSpec s;
  s.kind = ColourSpec::Kind::Palette256;
  s.paletteIndex = index;
  return s;
}

static ColourSpec MakeTrueColorSpec(std::uint32_t rgb) {
  ColourSpec s;
  s.kind = ColourSpec::Kind::TrueColor;
  s.rgb = rgb;
  return s;
}

std::size_t TerminalCore::AbsRow(std::size_t viewportRow) const {
  return m_shellStart + viewportRow;
}

std::optional<std::size_t> TerminalCore::ViewPortRow(std::size_t absrow) const {
  if (absrow < m_viewStart || absrow >= m_viewStart + m_rows)
    return std::nullopt;

  return absrow - m_viewStart;
}

wxPoint TerminalCore::Cursor() const { return m_cursor; }

const std::vector<Cell> &TerminalCore::BufferRow(std::size_t absRow) const {
  static const std::vector<Cell> empty;
  if (absRow < m_buffer.size())
    return m_buffer[absRow];
  return empty;
}

std::vector<const std::vector<Cell> *> TerminalCore::GetViewArea() const {
  std::vector<const std::vector<Cell> *> view(m_rows);
  for (std::size_t r = 0; r < m_rows; ++r)
    view[r] = &BufferRow(m_viewStart + r);
  return view;
}

void TerminalCore::SetViewStart(std::size_t vs) {
  std::size_t maxVs = m_buffer.size() > m_rows ? m_buffer.size() - m_rows : 0;
  m_viewStart = std::min(vs, maxVs);
}

void TerminalCore::Resize(std::size_t rows, std::size_t cols) {
  std::size_t newRows = std::max<std::size_t>(1, rows);
  std::size_t newCols = std::max<std::size_t>(1, cols);

  if (newRows == m_rows && newCols == m_cols)
    return;

  // If cols changed, resize all existing rows
  if (newCols != m_cols) {
    for (auto &row : m_buffer) {
      row.resize(newCols);
    }
  }

  // If viewport grew taller, ensure we have enough rows in the buffer
  if (newRows > m_rows) {
    while (m_buffer.size() < m_viewStart + newRows)
      m_buffer.push_back(std::vector<Cell>(newCols));
  }

  m_rows = newRows;
  m_cols = newCols;

  // Ensure viewStart is valid
  if (m_buffer.size() > m_rows) {
    m_shellStart = m_buffer.size() - m_rows;
    m_viewStart = m_shellStart;
  } else {
    m_shellStart = 0;
    m_viewStart = 0;
  }

  // Clamp cursor
  if (m_cursor.y >= m_rows)
    m_cursor.y = m_rows - 1;
  if (m_cursor.x >= m_cols)
    m_cursor.x = m_cols - 1;

  // Reset scroll region to full screen on resize
  m_scrollTop = 0;
  m_scrollBottom = m_rows - 1;
}

void TerminalCore::SetViewportSize(std::size_t rows, std::size_t cols) {
  Resize(rows, cols);
}

void TerminalCore::AppendLine(const std::string &line) {
  PutString(line);
  PutChar('\n');
}

void TerminalCore::ClearScreen() {
  for (std::size_t r = 0; r < m_rows; ++r) {
    std::size_t abs = AbsRow(r);
    if (abs < m_buffer.size()) {
      for (auto &cell : m_buffer[abs])
        cell = Cell{};
    }
  }
}

void TerminalCore::MoveCursor(std::size_t row, std::size_t col) {
  m_cursor.y = std::min(row, m_rows - 1);
  m_cursor.x = std::min(col, m_cols - 1);
}

void TerminalCore::Reset() {
  m_buffer.clear();
  for (std::size_t r = 0; r < m_rows; ++r)
    m_buffer.push_back(std::vector<Cell>(m_cols));
  m_viewStart = 0;
  m_shellStart = 0;
  m_cursor = {};
  m_scrollTop = 0;
  m_scrollBottom = m_rows - 1;
  m_savedCursor = {};
  m_lastChar = U' ';
  m_inEscape = false;
  m_escape.clear();
  m_attr = {};
}

void TerminalCore::PutData(const std::string &data) {
  TLOG_IF_TRACE { TLOG_TRACE() << "PutData len=" << data.size() << std::endl; }
  for (char c : data) {
    if (m_inEscape) {
      m_escape.push_back(c);

      // Safety: if escape buffer grows too large, it's stuck — dump and reset
      if (m_escape.size() > 256) {
        TLOG_WARN() << "Escape buffer overflow, reseting" << std::endl;
        m_escape.clear();
        m_inEscape = false;
        continue;
      }

      if (m_escape.size() > 0 && m_escape[0] == ']') {
        if (c == '\x07') {
          ParseEscape(m_escape);
          m_escape.clear();
          m_inEscape = false;
        } else if (m_escape.size() >= 2 &&
                   m_escape[m_escape.size() - 2] == '\x1b' && c == '\\') {
          ParseEscape(m_escape);
          m_escape.clear();
          m_inEscape = false;
        }
        continue;
      }

      if (c >= '@' && c <= '~') {
        if (m_escape.size() > 1 && m_escape[0] == '[') {
          ParseEscape(m_escape);
          m_escape.clear();
          m_inEscape = false;
        } else if (m_escape.size() == 1 && c != '[' && c != ']') {
          ParseEscape(m_escape);
          m_escape.clear();
          m_inEscape = false;
        } else if (m_escape.size() == 2 && m_escape[0] >= 0x20 &&
                   m_escape[0] <= 0x2F) {
          // Two-byte sequences with intermediate: ESC ( B, ESC ) 0, etc.
          ParseEscape(m_escape);
          m_escape.clear();
          m_inEscape = false;
        }
      } else if (c >= '0' && c <= '?') {
        if (m_escape.size() == 1 && m_escape[0] != '[' && m_escape[0] != ']') {
          ParseEscape(m_escape);
          m_escape.clear();
          m_inEscape = false;
        }
      }
      continue;
    }

    if (c == '\x1b') {
      if (!m_utf8Buf.empty()) {
        wxString ws = wxString::FromUTF8(m_utf8Buf);
        for (size_t i = 0; i < ws.length(); ++i)
          PutPrintable(static_cast<char32_t>(ws[i].GetValue()));
        m_utf8Buf.clear();
      }
      m_inEscape = true;
      m_escape.clear();
      continue;
    }

    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 0x20 && uc != 0x7f) {
      m_utf8Buf.push_back(c);
      continue;
    }

    if (!m_utf8Buf.empty()) {
      wxString ws = wxString::FromUTF8(m_utf8Buf);
      for (size_t i = 0; i < ws.length(); ++i) {
        PutPrintable(static_cast<char32_t>(ws[i].GetValue()));
      }
      m_utf8Buf.clear();
    }
    PutChar(c);
  }

  if (!m_utf8Buf.empty()) {
    wxString ws = wxString::FromUTF8(m_utf8Buf);
    for (size_t i = 0; i < ws.length(); ++i)
      PutPrintable(static_cast<char32_t>(ws[i].GetValue()));
    m_utf8Buf.clear();
  }
}

void TerminalCore::PutChar(char c) {
  unsigned char uc = static_cast<unsigned char>(c);
  if (uc < 0x20 && c != '\n' && c != '\r' && c != '\b' && c != '\t')
    return;
  switch (c) {
  case '\n':
    NewLine();
    break;
  case '\r':
    CarriageReturn();
    break;
  case '\b':
    Backspace();
    break;
  case '\t':
    Tab();
    break;
  default:
    PutPrintable(c);
    break;
  }
}

void TerminalCore::PutString(const std::string &text) {
  for (char c : text)
    PutChar(c);
}

void TerminalCore::PutPrintable(char c) { PutCell(c); }
void TerminalCore::PutPrintable(char32_t cp) { PutCell(cp); }

void TerminalCore::PutCell(char32_t cp) {
  if (m_cursor.x >= m_cols) {
    m_cursor.x = 0;
    NewLine();
    CarriageReturn();
  }

  std::size_t abs = AbsRow(m_cursor.y);
  if (abs < m_buffer.size() && m_cursor.x < m_cols) {
    auto cell = m_attr;
    cell.ch = cp;
    m_buffer[abs][m_cursor.x] = cell;
    m_lastChar = cp;
    ++m_cursor.x;
  }
}

void TerminalCore::NewLine() {
  if (m_cursor.y == m_scrollBottom) {
    // Full-screen scroll region: grow the buffer (preserves scrollback)
    if (m_scrollTop == 0 && m_scrollBottom == m_rows - 1)
      ScrollUp();
    else
      ScrollRegionUp();
  } else if (m_cursor.y >= m_rows - 1) {
    ScrollUp();
  } else {
    ++m_cursor.y;
  }
}

void TerminalCore::CarriageReturn() { m_cursor.x = 0; }

void TerminalCore::Backspace() {
  if (m_cursor.x > 0)
    --m_cursor.x;
}

void TerminalCore::Tab() {
  m_cursor.x =
      std::min(static_cast<int>(m_cols - 1), ((m_cursor.x / 8) + 1) * 8);
}

void TerminalCore::ScrollUp() {
  m_buffer.push_back(std::vector<Cell>(m_cols));
  ++m_shellStart;
  if (m_viewStart + m_rows >= m_buffer.size() - 1)
    m_viewStart = m_shellStart;
  if (m_buffer.size() > m_maxLines) {
    m_buffer.pop_front();
    --m_shellStart;
    if (m_viewStart > 0)
      --m_viewStart;
  }
}

void TerminalCore::ScrollRegionUp() {
  // Delete the top row of the scroll region, shift rows up, blank the bottom
  std::size_t top = AbsRow(m_scrollTop);
  std::size_t bot = AbsRow(m_scrollBottom);
  if (top >= m_buffer.size() || bot >= m_buffer.size())
    return;
  for (std::size_t r = top; r < bot; ++r)
    m_buffer[r] = m_buffer[r + 1];
  m_buffer[bot] = std::vector<Cell>(m_cols);
}

void TerminalCore::ScrollRegionDown() {
  // Insert a blank row at the top of the scroll region, shift rows down
  std::size_t top = AbsRow(m_scrollTop);
  std::size_t bot = AbsRow(m_scrollBottom);
  if (top >= m_buffer.size() || bot >= m_buffer.size())
    return;
  for (std::size_t r = bot; r > top; --r)
    m_buffer[r] = m_buffer[r - 1];
  m_buffer[top] = std::vector<Cell>(m_cols);
}

void TerminalCore::ParseEscape(const std::string &seq) {
  if (seq.empty())
    return;

  TLOG_IF_TRACE {
    std::ostringstream oss;
    oss << "ESC [";
    for (unsigned char ch : seq)
      oss << std::hex << std::setfill('0') << std::setw(2) << (int)ch << " ";
    oss << "] cursor=(" << std::dec << m_cursor.y << "," << m_cursor.x << ")";
    TLOG_TRACE() << oss.str() << std::endl;
  }

  if (seq.size() == 1) {
    switch (seq[0]) {
    case 'M': // Reverse Index — cursor up, scroll down if at top
      if (m_cursor.y == m_scrollTop)
        ScrollRegionDown();
      else if (m_cursor.y > 0)
        --m_cursor.y;
      break;
    case 'D': // Index — cursor down, scroll up if at bottom
      if (m_cursor.y == m_scrollBottom)
        ScrollRegionUp();
      else if (m_cursor.y < m_rows - 1)
        ++m_cursor.y;
      break;
    case 'E': // Next Line
      m_cursor.x = 0;
      if (m_cursor.y == m_scrollBottom)
        ScrollRegionUp();
      else if (m_cursor.y < m_rows - 1)
        ++m_cursor.y;
      break;
    case '7': // Save Cursor (DECSC)
      m_savedCursor = m_cursor;
      break;
    case '8': // Restore Cursor (DECRC)
      m_cursor = m_savedCursor;
      if (m_cursor.y >= m_rows)
        m_cursor.y = m_rows - 1;
      if (m_cursor.x >= m_cols)
        m_cursor.x = m_cols - 1;
      break;
    default:
      break;
    }
    return;
  }

  if (seq[0] == ']') {
    if (seq.size() > 3 && seq[1] == '1' && seq[2] == '1' && seq[3] == ';') {
      if (seq.find('?') != std::string::npos && m_responseCallback) {
        m_responseCallback(OscRgbResponse(m_theme.bg, 11));
      }
      return;
    }
    if (seq.size() > 3 && seq[1] == '1' && seq[2] == '0' && seq[3] == ';') {
      if (seq.find('?') != std::string::npos && m_responseCallback) {
        m_responseCallback(OscRgbResponse(m_theme.fg, 10));
      }
      return;
    }
    if (seq.size() > 2 && (seq[1] == '0' || seq[1] == '2') && seq[2] == ';') {
      std::string title = seq.substr(3);
      if (!title.empty() && (title.back() == '\x07' || title.back() == '\\'))
        title.pop_back();
      if (!title.empty() && title.back() == '\x1b')
        title.pop_back();
      if (m_titleCallback)
        m_titleCallback(title);
    }
    return;
  }

  if (seq[0] != '[')
    return;

  const char final_ch = seq.back();

  std::size_t paramStart = 1;
  bool privateMode = false;
  if (seq.size() > 1 &&
      (seq[1] == '?' || seq[1] == '>' || seq[1] == '<' || seq[1] == '=')) {
    paramStart = 2;
    privateMode = true;
  }
  const std::string params =
      (seq.size() > paramStart)
          ? seq.substr(paramStart, seq.size() - paramStart - 1)
          : "";

  std::vector<int> paramList;
  if (!params.empty()) {
    std::stringstream ss(params);
    std::string token;
    while (std::getline(ss, token, ';')) {
      try {
        paramList.push_back(token.empty() ? 0 : std::stoi(token));
      } catch (...) {
        paramList.push_back(0);
      }
    }
  }

  if (privateMode) {
    bool setMode = (final_ch == 'h');
    for (int mode : paramList) {
      switch (mode) {
      case 1049: // Alternate screen buffer with save/restore cursor
      case 47:   // Alternate screen buffer (no cursor save)
        if (setMode && !m_altScreenActive) {
          // Save current screen and switch to alt
          m_savedScreen.buffer = m_buffer;
          m_savedScreen.viewStart = m_viewStart;
          m_savedScreen.shellStart = m_shellStart;
          m_savedScreen.cursor = m_cursor;
          m_savedScreen.scrollTop = m_scrollTop;
          m_savedScreen.scrollBottom = m_scrollBottom;
          m_altScreenActive = true;
          // Clear alt screen
          m_buffer.clear();
          for (std::size_t r = 0; r < m_rows; ++r)
            m_buffer.push_back(std::vector<Cell>(m_cols));
          m_viewStart = 0;
          m_shellStart = 0;
          m_cursor = {};
          m_scrollTop = 0;
          m_scrollBottom = m_rows - 1;
        } else if (!setMode && m_altScreenActive) {
          // Restore main screen
          m_buffer = m_savedScreen.buffer;
          m_viewStart = m_savedScreen.viewStart;
          m_shellStart = m_savedScreen.shellStart;
          m_cursor = m_savedScreen.cursor;
          m_scrollTop = m_savedScreen.scrollTop;
          m_scrollBottom = m_savedScreen.scrollBottom;
          m_altScreenActive = false;
        }
        break;
      case 25: // Show/hide cursor — acknowledged, rendering handles this
        break;
      case 1: // Application/normal cursor keys — acknowledged
        break;
      case 7: // Auto-wrap — acknowledged
        break;
      default:
        break;
      }
    }
    return;
  }

  if (final_ch == 'c') {
    if (m_responseCallback)
      m_responseCallback("\x1b[?1;0c");
    return;
  }

  if (final_ch == 'n') {
    if (!paramList.empty() && paramList[0] == 6 && m_responseCallback) {
      m_responseCallback("\x1b[" + std::to_string(m_cursor.y + 1) + ";" +
                         std::to_string(m_cursor.x + 1) + "R");
    }
    return;
  }

  // Helper to get absolute row for cursor
  auto cursorAbsRow = [&]() { return AbsRow(m_cursor.y); };

  switch (final_ch) {
  case 'm':
    ApplySgr(params);
    break;

  case 'A': {
    std::size_t n = paramList.empty()
                        ? 1
                        : static_cast<std::size_t>(std::max(1, paramList[0]));
    m_cursor.y = (m_cursor.y > n) ? (m_cursor.y - n) : 0;
    break;
  }
  case 'B': {
    std::size_t n = paramList.empty()
                        ? 1
                        : static_cast<std::size_t>(std::max(1, paramList[0]));
    m_cursor.y = std::min(m_rows - 1, m_cursor.y + n);
    break;
  }
  case 'C': {
    std::size_t n = paramList.empty()
                        ? 1
                        : static_cast<std::size_t>(std::max(1, paramList[0]));
    m_cursor.x = std::min(m_cols - 1, m_cursor.x + n);
    break;
  }
  case 'D': {
    std::size_t n = paramList.empty()
                        ? 1
                        : static_cast<std::size_t>(std::max(1, paramList[0]));
    m_cursor.x = (m_cursor.x > n) ? (m_cursor.x - n) : 0;
    break;
  }
  case 'E': {
    std::size_t n = paramList.empty()
                        ? 1
                        : static_cast<std::size_t>(std::max(1, paramList[0]));
    m_cursor.y = std::min(m_rows - 1, m_cursor.y + n);
    m_cursor.x = 0;
    break;
  }
  case 'F': {
    std::size_t n = paramList.empty()
                        ? 1
                        : static_cast<std::size_t>(std::max(1, paramList[0]));
    m_cursor.y = (m_cursor.y > n) ? (m_cursor.y - n) : 0;
    m_cursor.x = 0;
    break;
  }
  case 'G': {
    std::size_t col = paramList.empty()
                          ? 1
                          : static_cast<std::size_t>(std::max(1, paramList[0]));
    m_cursor.x = std::min(m_cols - 1, col - 1);
    break;
  }
  case 'H':
  case 'f': {
    std::size_t row = 0, col = 0;
    if (paramList.size() >= 1 && paramList[0] > 0)
      row = paramList[0] - 1;
    if (paramList.size() >= 2 && paramList[1] > 0)
      col = paramList[1] - 1;
    MoveCursor(row, col);
    break;
  }

  case 'J': {
    int mode = paramList.empty() ? 0 : paramList[0];
    if (mode == 0) {
      std::size_t abs = cursorAbsRow();
      if (abs < m_buffer.size())
        for (std::size_t c = m_cursor.x; c < m_cols; ++c)
          m_buffer[abs][c] = Cell{};
      for (std::size_t r = m_cursor.y + 1; r < m_rows; ++r) {
        std::size_t a = AbsRow(r);
        if (a < m_buffer.size())
          for (std::size_t c = 0; c < m_cols; ++c)
            m_buffer[a][c] = Cell{};
      }
    } else if (mode == 1) {
      for (std::size_t r = 0; r < m_cursor.y; ++r) {
        std::size_t a = AbsRow(r);
        if (a < m_buffer.size())
          for (std::size_t c = 0; c < m_cols; ++c)
            m_buffer[a][c] = Cell{};
      }
      std::size_t abs = cursorAbsRow();
      if (abs < m_buffer.size())
        for (std::size_t c = 0; c <= m_cursor.x && c < m_cols; ++c)
          m_buffer[abs][c] = Cell{};
    } else if (mode == 2) {
      ClearScreen();
    } else if (mode == 3) {
      ClearScreen();
      while (m_buffer.size() > m_rows)
        m_buffer.pop_front();
      m_viewStart = 0;
      m_shellStart = 0;
      m_cursor = {0, 0};
    }
    break;
  }

  case 'K': {
    int mode = paramList.empty() ? 0 : paramList[0];
    std::size_t abs = cursorAbsRow();
    if (abs >= m_buffer.size())
      break;
    auto &row = m_buffer[abs];
    if (mode == 0) {
      for (std::size_t c = m_cursor.x; c < m_cols; ++c)
        row[c] = Cell{};
    } else if (mode == 1) {
      for (std::size_t c = 0; c <= m_cursor.x && c < m_cols; ++c)
        row[c] = Cell{};
    } else if (mode == 2) {
      for (std::size_t c = 0; c < m_cols; ++c)
        row[c] = Cell{};
    }
    break;
  }

  case 'S': {
    std::size_t n =
        params.empty()
            ? 1
            : static_cast<std::size_t>(std::max(1, std::stoi(params)));
    for (std::size_t i = 0; i < n; ++i)
      ScrollUp();
    break;
  }

  case 'P': {
    std::size_t n = paramList.empty()
                        ? 1
                        : static_cast<std::size_t>(std::max(1, paramList[0]));
    std::size_t abs = cursorAbsRow();
    if (abs < m_buffer.size()) {
      auto &row = m_buffer[abs];
      for (std::size_t c = m_cursor.x; c + n < m_cols; ++c)
        row[c] = row[c + n];
      for (std::size_t c = m_cols > n ? m_cols - n : 0; c < m_cols; ++c)
        row[c] = Cell{};
    }
    break;
  }

  case '@': {
    std::size_t n = paramList.empty()
                        ? 1
                        : static_cast<std::size_t>(std::max(1, paramList[0]));
    std::size_t abs = cursorAbsRow();
    if (abs < m_buffer.size()) {
      auto &row = m_buffer[abs];
      for (std::size_t c = m_cols - 1; c >= m_cursor.x + n; --c)
        row[c] = row[c - n];
      for (std::size_t c = m_cursor.x; c < m_cursor.x + n && c < m_cols; ++c)
        row[c] = Cell{};
    }
    break;
  }

  case 'T': { // Scroll Down
    std::size_t n = paramList.empty()
                        ? 1
                        : static_cast<std::size_t>(std::max(1, paramList[0]));
    for (std::size_t i = 0; i < n; ++i)
      ScrollRegionDown();
    break;
  }

  case 'L': { // Insert Lines
    std::size_t n = paramList.empty()
                        ? 1
                        : static_cast<std::size_t>(std::max(1, paramList[0]));
    for (std::size_t i = 0; i < n; ++i) {
      // Shift rows from cursor down to scroll bottom
      std::size_t bot = AbsRow(m_scrollBottom);
      std::size_t cur = AbsRow(m_cursor.y);
      if (cur < m_buffer.size() && bot < m_buffer.size()) {
        for (std::size_t r = bot; r > cur; --r)
          m_buffer[r] = m_buffer[r - 1];
        m_buffer[cur] = std::vector<Cell>(m_cols);
      }
    }
    break;
  }

  case 'M': { // Delete Lines
    std::size_t n = paramList.empty()
                        ? 1
                        : static_cast<std::size_t>(std::max(1, paramList[0]));
    for (std::size_t i = 0; i < n; ++i) {
      std::size_t bot = AbsRow(m_scrollBottom);
      std::size_t cur = AbsRow(m_cursor.y);
      if (cur < m_buffer.size() && bot < m_buffer.size()) {
        for (std::size_t r = cur; r < bot; ++r)
          m_buffer[r] = m_buffer[r + 1];
        m_buffer[bot] = std::vector<Cell>(m_cols);
      }
    }
    break;
  }

  case 'X': { // Erase Characters (no shift)
    std::size_t n = paramList.empty()
                        ? 1
                        : static_cast<std::size_t>(std::max(1, paramList[0]));
    std::size_t abs = cursorAbsRow();
    if (abs < m_buffer.size()) {
      for (std::size_t c = m_cursor.x; c < m_cursor.x + n && c < m_cols; ++c)
        m_buffer[abs][c] = Cell{};
    }
    break;
  }

  case 's': // Save Cursor Position
    m_savedCursor = m_cursor;
    break;

  case 'u': // Restore Cursor Position
    m_cursor = m_savedCursor;
    if (m_cursor.y >= m_rows)
      m_cursor.y = m_rows - 1;
    if (m_cursor.x >= m_cols)
      m_cursor.x = m_cols - 1;
    break;

  case 'r': { // Set Scroll Region (DECSTBM)
    std::size_t top =
        paramList.size() >= 1 && paramList[0] > 0 ? paramList[0] - 1 : 0;
    std::size_t bot = paramList.size() >= 2 && paramList[1] > 0
                          ? paramList[1] - 1
                          : m_rows - 1;
    if (top < m_rows && bot < m_rows && top < bot) {
      m_scrollTop = top;
      m_scrollBottom = bot;
    }
    m_cursor.y = 0;
    m_cursor.x = 0;
    break;
  }

  case 'g': // Tab Clear (ignored for now — no custom tab stops)
    break;

  case 'd': { // Line Position Absolute (VPA)
    std::size_t row = paramList.empty()
                          ? 1
                          : static_cast<std::size_t>(std::max(1, paramList[0]));
    m_cursor.y = std::min(m_rows - 1, row - 1);
    break;
  }

  case 'b': { // Repeat Previous Character
    std::size_t n = paramList.empty()
                        ? 1
                        : static_cast<std::size_t>(std::max(1, paramList[0]));
    for (std::size_t i = 0; i < n; ++i)
      PutCell(m_lastChar);
    break;
  }

  default:
    break;
  }
}

void TerminalCore::ApplySgr(const std::string &params) {
  if (params.empty()) {
    m_attr = Cell::New(m_theme);
    return;
  }

  std::vector<int> codes;
  std::size_t start = 0;
  while (start <= params.size()) {
    const std::size_t semi = params.find(';', start);
    const std::size_t colon = params.find(':', start);
    const std::size_t end = std::min(semi, colon);
    const std::string token = params.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    int value = 0;
    if (!token.empty()) {
      try {
        value = std::stoi(token);
      } catch (...) {
      }
    }
    codes.push_back(value);
    if (end == std::string::npos)
      break;
    start = end + 1;
  }

  for (std::size_t i = 0; i < codes.size(); ++i) {
    int code = codes[i];
    switch (code) {
    case 0:
      m_attr = Cell::New(m_theme);
      break;
    case 1:
      m_attr.bold = true;
      break;
    case 4:
      m_attr.underline = true;
      break;
    case 7:
      m_attr.reverse = true;
      break;
    case 22:
      m_attr.bold = false;
      break;
    case 24:
      m_attr.underline = false;
      break;
    case 27:
      m_attr.reverse = false;
      break;
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
      m_attr.SetFgColour(MakeAnsiSpec(code - 30, false));
      break;
    case 90:
    case 91:
    case 92:
    case 93:
    case 94:
    case 95:
    case 96:
    case 97:
      m_attr.SetFgColour(MakeAnsiSpec(code - 90, true));
      break;
    case 40:
    case 41:
    case 42:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47:
      m_attr.SetBgColour(MakeAnsiSpec(code - 40, false));
      break;
    case 100:
    case 101:
    case 102:
    case 103:
    case 104:
    case 105:
    case 106:
    case 107:
      m_attr.SetBgColour(MakeAnsiSpec(code - 100, true));
      break;
    case 38:
      if (i + 1 < codes.size()) {
        if (codes[i + 1] == 5 && i + 2 < codes.size()) {
          m_attr.SetFgColour(MakePaletteSpec(codes[i + 2]));
          i += 2;
        } else if (codes[i + 1] == 2 && i + 4 < codes.size()) {
          m_attr.SetFgColour(MakeTrueColorSpec(
              (codes[i + 2] << 16) | (codes[i + 3] << 8) | codes[i + 4]));
          i += 4;
        }
      }
      break;
    case 48:
      if (i + 1 < codes.size()) {
        if (codes[i + 1] == 5 && i + 2 < codes.size()) {
          m_attr.SetBgColour(MakePaletteSpec(codes[i + 2]));
          i += 2;
        } else if (codes[i + 1] == 2 && i + 4 < codes.size()) {
          m_attr.SetBgColour(MakeTrueColorSpec(
              (codes[i + 2] << 16) | (codes[i + 3] << 8) | codes[i + 4]));
          i += 4;
        }
      }
      break;
    case 39:
      if (!m_attr.colours)
        m_attr.colours = CellColours{};
      m_attr.colours->fg = std::nullopt;
      break;
    case 49:
      if (!m_attr.colours)
        m_attr.colours = CellColours{};
      m_attr.colours->bg = std::nullopt;
      break;
    default:
      break;
    }
  }
}

std::string TerminalCore::Flatten() const {
  std::string out;
  for (std::size_t r = 0; r < m_rows; ++r) {
    std::size_t abs = m_viewStart + r;
    if (abs < m_buffer.size()) {
      for (const auto &cell : m_buffer[abs])
        out.push_back(static_cast<char>(cell.ch));
    }
    out.push_back('\n');
  }
  return out;
}

} // namespace terminal
