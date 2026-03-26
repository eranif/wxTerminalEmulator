#include "terminal_core.h"

#include <algorithm>
#include <sstream>

#include <wx/string.h>

#include <wx/string.h>

namespace terminal {

namespace {

// ANSI 16-color palette (standard colors)
std::uint32_t GetAnsiColor(int index, bool bright = false) {
  static const std::uint32_t normal[8] = {
      0x000000, // Black
      0x800000, // Red
      0x008000, // Green
      0x808000, // Yellow
      0x000080, // Blue
      0x800080, // Magenta
      0x008080, // Cyan
      0xC0C0C0  // White
  };

  static const std::uint32_t bright_colors[8] = {
      0x808080, // Bright Black (Gray)
      0xFF0000, // Bright Red
      0x00FF00, // Bright Green
      0xFFFF00, // Bright Yellow
      0x0000FF, // Bright Blue
      0xFF00FF, // Bright Magenta
      0x00FFFF, // Bright Cyan
      0xFFFFFF  // Bright White
  };

  if (index < 0 || index > 7)
    return 0xC0C0C0;
  return bright ? bright_colors[index] : normal[index];
}

// Convert 256-color palette index to RGB
std::uint32_t Get256Color(int index) {
  if (index < 16) {
    return GetAnsiColor(index % 8, index >= 8);
  } else if (index < 232) {
    // 216-color cube (6x6x6)
    int idx = index - 16;
    int r = (idx / 36) * 51;
    int g = ((idx / 6) % 6) * 51;
    int b = (idx % 6) * 51;
    return (r << 16) | (g << 8) | b;
  } else {
    // Grayscale ramp
    int gray = 8 + (index - 232) * 10;
    return (gray << 16) | (gray << 8) | gray;
  }
}

} // anonymous namespace

TerminalCore::TerminalCore(std::size_t rows, std::size_t cols,
                           std::size_t scrollback)
    : m_rows(rows), m_cols(cols), m_scrollbackLimit(scrollback) {
  Reset();
}

void TerminalCore::Resize(std::size_t rows, std::size_t cols) {
  std::size_t newRows = std::max<std::size_t>(1, rows);
  std::size_t newCols = std::max<std::size_t>(1, cols);

  // If size hasn't changed, nothing to do
  if (newRows == m_rows && newCols == m_cols) {
    return;
  }

  // Save the old screen content
  std::vector<std::vector<Cell>> oldScreen = std::move(m_screen);
  std::size_t oldRows = m_rows;
  std::size_t oldCols = m_cols;

  // Update dimensions
  m_rows = newRows;
  m_cols = newCols;

  // Create new screen buffer
  m_screen.assign(m_rows, std::vector<Cell>(m_cols));

  // Copy old content to new screen, preserving as much as possible
  std::size_t rowsToCopy = std::min(oldRows, newRows);
  for (std::size_t r = 0; r < rowsToCopy; ++r) {
    std::size_t colsToCopy = std::min(oldCols, newCols);
    for (std::size_t c = 0; c < colsToCopy; ++c) {
      m_screen[r][c] = oldScreen[r][c];
    }
  }

  // If we grew wider, fill new columns with spaces
  // If we grew taller, new rows are already initialized with spaces

  // Adjust cursor position to be within new bounds
  if (m_cursor.row >= newRows) {
    m_cursor.row = (newRows > 0) ? newRows - 1 : 0;
  }
  if (m_cursor.col >= newCols) {
    m_cursor.col = (newCols > 0) ? newCols - 1 : 0;
  }
}

void TerminalCore::SetViewportSize(std::size_t rows, std::size_t cols) {
  Resize(rows, cols);
}

void TerminalCore::AppendLine(const std::string &line) {
  PutString(line);
  PutChar('\n');
}

void TerminalCore::ClearScreen() {
  for (auto &row : m_screen) {
    for (auto &cell : row) {
      cell.ch = U' ';
    }
  }
}

void TerminalCore::MoveCursor(std::size_t row, std::size_t col) {
  m_cursor.row = std::min(row, m_rows - 1);
  m_cursor.col = std::min(col, m_cols - 1);
}

void TerminalCore::Reset() {
  m_screen.assign(m_rows, std::vector<Cell>(m_cols));
  m_cursor = {};
  m_inEscape = false;
  m_escape.clear();
  m_attr = Cell{};
}

void TerminalCore::PutData(const std::string &data) {
  for (char c : data) {
    if (m_inEscape) {
      m_escape.push_back(c);

      // OSC sequences (]...) are terminated by BEL (0x07) or ESC \ (ST)
      if (m_escape.size() > 0 && m_escape[0] == ']') {
        if (c == '\x07') {
          // BEL terminator
          ParseEscape(m_escape);
          m_escape.clear();
          m_inEscape = false;
        } else if (m_escape.size() >= 2 &&
                   m_escape[m_escape.size() - 2] == '\x1b' && c == '\\') {
          // ESC \ terminator (String Terminator)
          ParseEscape(m_escape);
          m_escape.clear();
          m_inEscape = false;
        }
        continue;
      }

      // Check for escape sequence terminator
      // Final byte is in range 0x40-0x7E for CSI sequences
      // But also 0x30-0x3F for some single-char sequences like ESC> ESC=
      if (c >= '@' && c <= '~') {
        // Check we have proper CSI sequence
        if (m_escape.size() > 1 && m_escape[0] == '[') {
          // CSI sequence - parse it
          ParseEscape(m_escape);
          m_escape.clear();
          m_inEscape = false;
        } else if (m_escape.size() == 1 && c != '[' && c != ']' &&
                   (c >= '@' && c <= '~')) {
          // Single character escape sequence (like ESC 7, ESC 8, etc.)
          // But NOT '[' or ']' which start CSI/OSC sequences
          m_escape.push_back(c);
          ParseEscape(m_escape);
          m_escape.clear();
          m_inEscape = false;
        }
      } else if (c >= '0' && c <= '?') {
        // Single-byte sequences like ESC> ESC= ESC7 ESC8
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
    // Accumulate printable bytes (may be UTF-8 multi-byte)
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 0x20 && uc != 0x7f) {
      m_utf8Buf.push_back(c);
      continue;
    }
    // Control character — flush accumulated text first, then handle it
    if (!m_utf8Buf.empty()) {
      wxString ws = wxString::FromUTF8(m_utf8Buf);
      for (size_t i = 0; i < ws.length(); ++i)
        PutPrintable(static_cast<char32_t>(ws[i].GetValue()));
      m_utf8Buf.clear();
    }
    PutChar(c);
  }
  // Flush any remaining accumulated text
  if (!m_utf8Buf.empty()) {
    wxString ws = wxString::FromUTF8(m_utf8Buf);
    for (size_t i = 0; i < ws.length(); ++i)
      PutPrintable(static_cast<char32_t>(ws[i].GetValue()));
    m_utf8Buf.clear();
  }
}

void TerminalCore::PutChar(char c) {
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

void TerminalCore::PutCell(char c) {
  if (m_cursor.row >= m_screen.size())
    ScrollUp();
  if (m_cursor.row < m_screen.size() && m_cursor.col < m_cols) {
    auto cell = m_attr;
    cell.ch = static_cast<unsigned char>(c);
    m_screen[m_cursor.row][m_cursor.col] = cell;
    ++m_cursor.col;
    if (m_cursor.col >= m_cols)
      NewLine();
  }
}

void TerminalCore::PutPrintable(char c) { PutCell(c); }

void TerminalCore::PutPrintable(char32_t cp) { PutCell(cp); }

void TerminalCore::PutCell(char32_t cp) {
  if (m_cursor.row >= m_screen.size())
    ScrollUp();
  if (m_cursor.row < m_screen.size() && m_cursor.col < m_cols) {
    auto cell = m_attr;
    cell.ch = cp;
    m_screen[m_cursor.row][m_cursor.col] = cell;
    ++m_cursor.col;
    if (m_cursor.col >= m_cols)
      NewLine();
  }
}

void TerminalCore::NewLine() {
  // In standard VT100, LF (\n) only moves down one line, it doesn't reset
  // column CR (\r) resets column. Together \r\n gives the expected behavior.
  ++m_cursor.row;
  if (m_cursor.row >= m_rows) {
    ScrollUp();
    m_cursor.row = m_rows - 1;
  }
}

void TerminalCore::CarriageReturn() { m_cursor.col = 0; }
void TerminalCore::Backspace() {
  if (m_cursor.col > 0)
    --m_cursor.col;
}
void TerminalCore::Tab() {
  // Tab stops at 8-character intervals (standard terminal behavior)
  m_cursor.col = std::min(m_cols - 1, ((m_cursor.col / 8) + 1) * 8);
}

void TerminalCore::ScrollUp() {
  if (!m_screen.empty()) {
    std::string line;
    line.reserve(m_cols);
    for (const auto &cell : m_screen.front())
      line.push_back(static_cast<char>(cell.ch));
    m_scrollback.push_back(std::move(line));
    if (m_scrollback.size() > m_scrollbackLimit)
      m_scrollback.erase(m_scrollback.begin());
    m_screen.erase(m_screen.begin());
    m_screen.push_back(std::vector<Cell>(m_cols));
  }
}

void TerminalCore::ParseEscape(const std::string &seq) {
  if (seq.empty())
    return;

  // Handle single-character escape sequences (not CSI or OSC)
  if (seq.size() == 1) {
    switch (seq[0]) {
    case '7': // DECSC - Save cursor position
      // We could save cursor position here
      break;
    case '8': // DECRC - Restore cursor position
      // We could restore cursor position here
      break;
    case '>': // DECKPNM - Keypad Numeric Mode
      // Normal keypad mode - acknowledge
      break;
    case '=': // DECKPAM - Keypad Application Mode
      // Application keypad mode - acknowledge
      break;
    default:
      // Unknown single-char sequence - ignore
      break;
    }
    return;
  }

  // Handle OSC sequences (starts with ])
  if (seq[0] == ']') {
    // Operating System Command - just ignore for now
    return;
  }

  // Only handle CSI sequences (starts with [)
  if (seq[0] != '[')
    return;

  const char final = seq.back();

  // Extract params, skipping any private mode character (?, >, <, etc.)
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

  // Parse parameters into a vector
  std::vector<int> paramList;
  if (!params.empty()) {
    std::stringstream ss(params);
    std::string token;
    while (std::getline(ss, token, ';')) {
      if (!token.empty()) {
        try {
          paramList.push_back(std::stoi(token));
        } catch (...) {
          paramList.push_back(0);
        }
      } else {
        paramList.push_back(0);
      }
    }
  }

  // Private mode sequences (like ?25h for cursor visibility)
  if (privateMode) {
    // Handle important private mode sequences
    if (final == 'h' || final == 'l') {
      // Set (h) or Reset (l) mode
      bool enable = (final == 'h');

      for (int param : paramList) {
        switch (param) {
        case 25: // Cursor visibility
          // We always show cursor, but acknowledge the sequence
          break;
        case 1: // Application cursor keys
          // We send standard cursor keys, but acknowledge
          break;
        case 47:   // Alternative screen buffer (old)
        case 1047: // Alternative screen buffer
        case 1049: // Save cursor + Alternative screen
          // We don't implement alternate buffer, but acknowledge
          // This is used by less, vim, etc.
          break;
        }
      }
    }
    return; // Don't process further
  }

  // Handle device queries that need responses
  if (final == 'c') {
    // Device Attributes query - respond with VT100
    if (m_responseCallback) {
      m_responseCallback("\x1b[?1;0c"); // VT100 with no options
    }
    return;
  }

  if (final == 'n') {
    // Device Status Report
    if (!paramList.empty() && paramList[0] == 6) {
      // Cursor Position Report
      if (m_responseCallback) {
        m_responseCallback("\x1b[" + std::to_string(m_cursor.row + 1) + ";" +
                           std::to_string(m_cursor.col + 1) + "R");
      }
    }
    return;
  }

  switch (final) {
  case 'm': // SGR - Select Graphic Rendition
    ApplySgr(params);
    break;

  case 'A': { // Cursor Up
    const std::size_t n =
        paramList.empty() ? 1
                          : static_cast<std::size_t>(std::max(1, paramList[0]));
    m_cursor.row = (m_cursor.row > n) ? (m_cursor.row - n) : 0;
    break;
  }

  case 'B': { // Cursor Down
    const std::size_t n =
        paramList.empty() ? 1
                          : static_cast<std::size_t>(std::max(1, paramList[0]));
    m_cursor.row = std::min(m_rows - 1, m_cursor.row + n);
    break;
  }

  case 'C': { // Cursor Forward
    const std::size_t n =
        paramList.empty() ? 1
                          : static_cast<std::size_t>(std::max(1, paramList[0]));
    m_cursor.col = std::min(m_cols - 1, m_cursor.col + n);
    break;
  }

  case 'D': { // Cursor Back
    const std::size_t n =
        paramList.empty() ? 1
                          : static_cast<std::size_t>(std::max(1, paramList[0]));
    m_cursor.col = (m_cursor.col > n) ? (m_cursor.col - n) : 0;
    break;
  }

  case 'E': { // Cursor Next Line
    const std::size_t n =
        paramList.empty() ? 1
                          : static_cast<std::size_t>(std::max(1, paramList[0]));
    m_cursor.row = std::min(m_rows - 1, m_cursor.row + n);
    m_cursor.col = 0;
    break;
  }

  case 'F': { // Cursor Previous Line
    const std::size_t n =
        paramList.empty() ? 1
                          : static_cast<std::size_t>(std::max(1, paramList[0]));
    m_cursor.row = (m_cursor.row > n) ? (m_cursor.row - n) : 0;
    m_cursor.col = 0;
    break;
  }

  case 'G': { // Cursor Horizontal Absolute
    const std::size_t col =
        paramList.empty() ? 1
                          : static_cast<std::size_t>(std::max(1, paramList[0]));
    m_cursor.col = std::min(m_cols - 1, col - 1);
    break;
  }

  case 'H':   // Cursor Position
  case 'f': { // Horizontal Vertical Position
    std::size_t row = 0;
    std::size_t col = 0;
    if (paramList.size() >= 1 && paramList[0] > 0)
      row = paramList[0] - 1;
    if (paramList.size() >= 2 && paramList[1] > 0)
      col = paramList[1] - 1;
    MoveCursor(row, col);
    break;
  }

  case 'J': { // Erase in Display
    int mode = paramList.empty() ? 0 : paramList[0];
    if (mode == 0) {
      // Clear from cursor to end of screen
      for (std::size_t c = m_cursor.col; c < m_cols; ++c) {
        if (m_cursor.row < m_screen.size())
          m_screen[m_cursor.row][c].ch = U' ';
      }
      for (std::size_t r = m_cursor.row + 1; r < m_rows; ++r) {
        for (std::size_t c = 0; c < m_cols; ++c) {
          m_screen[r][c].ch = U' ';
        }
      }
    } else if (mode == 1) {
      // Clear from cursor to beginning of screen
      for (std::size_t r = 0; r < m_cursor.row && r < m_rows; ++r) {
        for (std::size_t c = 0; c < m_cols; ++c) {
          m_screen[r][c].ch = U' ';
        }
      }
      for (std::size_t c = 0; c <= m_cursor.col && c < m_cols; ++c) {
        if (m_cursor.row < m_screen.size())
          m_screen[m_cursor.row][c].ch = U' ';
      }
    } else if (mode == 2 || mode == 3) {
      // Clear entire screen (mode 3 also clears scrollback)
      ClearScreen();
      if (mode == 3) {
        m_scrollback.clear();
      }
    }
    break;
  }

  case 'K': { // Erase in Line
    int mode = paramList.empty() ? 0 : paramList[0];

    if (m_cursor.row >= m_screen.size())
      break;

    if (mode == 0) {
      // Clear from cursor to end of line
      for (std::size_t c = m_cursor.col; c < m_cols; ++c) {
        m_screen[m_cursor.row][c].ch = U' ';
      }
    } else if (mode == 1) {
      // Clear from cursor to beginning of line
      for (std::size_t c = 0; c <= m_cursor.col && c < m_cols; ++c) {
        m_screen[m_cursor.row][c].ch = U' ';
      }
    } else if (mode == 2) {
      // Clear entire line
      for (std::size_t c = 0; c < m_cols; ++c) {
        m_screen[m_cursor.row][c].ch = U' ';
      }
    }
    break;
  }

  case 'S': { // Scroll Up
    const std::size_t n =
        params.empty()
            ? 1
            : static_cast<std::size_t>(std::max(1, std::stoi(params)));
    for (std::size_t i = 0; i < n; ++i) {
      ScrollUp();
    }
    break;
  }

  default:
    // Unhandled escape sequence
    break;
  }
}

void TerminalCore::ApplySgr(const std::string &params) {
  if (params.empty()) {
    m_attr = Cell{};
    return;
  }

  // Build a flat list of codes, treating both ';' and ':' as separators.
  // The colon form (e.g. 38:5:75) is the ISO 8613-6 sub-parameter syntax
  // and is functionally equivalent to the semicolon form (38;5;75) for our
  // purposes.
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
        value = 0;
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
    case 0: // Reset
      m_attr = Cell{};
      break;

    case 1: // Bold
      m_attr.bold = true;
      break;

    case 4: // Underline
      m_attr.underline = true;
      break;

    case 7: // Reverse video
      m_attr.reverse = true;
      break;

    case 22: // Normal intensity
      m_attr.bold = false;
      break;

    case 24: // Not underlined
      m_attr.underline = false;
      break;

    case 27: // Not reversed
      m_attr.reverse = false;
      break;

    // Foreground colors (30-37: normal, 90-97: bright)
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
      m_attr.fg = GetAnsiColor(code - 30, false);
      break;

    case 90:
    case 91:
    case 92:
    case 93:
    case 94:
    case 95:
    case 96:
    case 97:
      m_attr.fg = GetAnsiColor(code - 90, true);
      break;

    // Background colors (40-47: normal, 100-107: bright)
    case 40:
    case 41:
    case 42:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47:
      m_attr.bg = GetAnsiColor(code - 40, false);
      break;

    case 100:
    case 101:
    case 102:
    case 103:
    case 104:
    case 105:
    case 106:
    case 107:
      m_attr.bg = GetAnsiColor(code - 100, true);
      break;

    case 38: // Set foreground color (extended)
      if (i + 1 < codes.size()) {
        if (codes[i + 1] == 5 && i + 2 < codes.size()) {
          // 256-color mode: ESC[38;5;<n>m
          m_attr.fg = Get256Color(codes[i + 2]);
          i += 2;
        } else if (codes[i + 1] == 2 && i + 4 < codes.size()) {
          // RGB mode: ESC[38;2;<r>;<g>;<b>m
          int r = codes[i + 2];
          int g = codes[i + 3];
          int b = codes[i + 4];
          m_attr.fg = (r << 16) | (g << 8) | b;
          i += 4;
        }
      }
      break;

    case 48: // Set background color (extended)
      if (i + 1 < codes.size()) {
        if (codes[i + 1] == 5 && i + 2 < codes.size()) {
          // 256-color mode: ESC[48;5;<n>m
          m_attr.bg = Get256Color(codes[i + 2]);
          i += 2;
        } else if (codes[i + 1] == 2 && i + 4 < codes.size()) {
          // RGB mode: ESC[48;2;<r>;<g>;<b>m
          int r = codes[i + 2];
          int g = codes[i + 3];
          int b = codes[i + 4];
          m_attr.bg = (r << 16) | (g << 8) | b;
          i += 4;
        }
      }
      break;

    case 39: // Default foreground color
      m_attr.fg = 0x00C0C0C0;
      break;

    case 49: // Default background color
      m_attr.bg = 0x00000000;
      break;

    default:
      break;
    }
  }
}

std::string TerminalCore::Flatten() const {
  std::string out;
  for (const auto &row : m_screen) {
    for (const auto &cell : row)
      out.push_back(static_cast<char>(cell.ch));
    out.push_back('\n');
  }
  return out;
}

} // namespace terminal
