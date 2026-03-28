#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <wx/gdicmn.h>

#include "terminal_theme.h"

namespace terminal {

struct ColourIndex {
  int index{-1};
  bool bright{false};
};

struct ColourSpec {
  enum class Kind { Default, Ansi, Palette256, TrueColor } kind{Kind::Default};
  ColourIndex ansi{};
  int paletteIndex{-1};
  std::uint32_t rgb{0};
};

struct CellColours {
  std::optional<ColourSpec> bg{std::nullopt};
  std::optional<ColourSpec> fg{std::nullopt};
};

/**
 * @brief Converts a 24-bit packed RGB value into a wxColour.
 *
 * Creates a wxColour from the red, green, and blue components stored in the
 * lower 24 bits of the input value, using the standard 0xRRGGBB layout.
 *
 * @param c std::uint32_t Packed RGB color value in 0xRRGGBB format.
 *
 * @return wxColour The corresponding color object constructed from the
 *         extracted red, green, and blue components.
 */
inline wxColour ToColour(std::uint32_t c) {
  return wxColour((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

struct ThemeColourMap {
  wxColour fg;
  wxColour bg;
  wxColour ansi[8];
  wxColour ansiBright[8];
};

struct Cell {
  char32_t ch{U' '};
  std::optional<CellColours> colours{std::nullopt};
  bool bold{false};
  bool underline{false};
  bool reverse{false};

  inline bool IsEmpty() const { return ch == U' ' && !colours.has_value(); }
  inline void SetColours(const wxTerminalTheme &theme) {
    SetColours(theme.bg, theme.fg);
  }

  inline void SetColours(const wxColour &bg, const wxColour &fg) {
    colours = CellColours{};
  }

  inline void SetFgColour(ColourSpec c) {
    if (IsEmpty()) {
      colours = CellColours{};
    }
    colours.value().fg = c;
  }

  inline void SetBgColour(ColourSpec c) {
    if (IsEmpty()) {
      colours = CellColours{};
    }
    colours.value().bg = c;
  }

  /**
   * @brief Creates a new Cell initialized with the given character and theme
   * colors.
   *
   * Constructs a Cell, assigns its character value, applies the colors from the
   * provided wxTerminalTheme, and returns the initialized object.
   *
   * @param theme const wxTerminalTheme & The theme used to initialize the
   * Cell's colours.
   * @param c char32_t The character to store in the new Cell.
   *
   * @return Cell The newly created and fully initialized Cell.
   */
  inline static Cell New(const wxTerminalTheme &theme, char32_t c = U' ') {
    Cell cell;
    cell.ch = c;
    cell.SetColours(theme);
    return cell;
  }
};

class TerminalCore {
public:
  TerminalCore(std::size_t rows = 24, std::size_t cols = 80,
               std::size_t maxLines = 10000);

  void SetTheme(const wxTerminalTheme &theme);
  const wxTerminalTheme &GetTheme() const { return m_theme; }

  void Resize(std::size_t rows, std::size_t cols);
  void Reset();
  void PutData(const std::string &data);
  void SetViewportSize(std::size_t rows, std::size_t cols);
  void AppendLine(const std::string &line);
  void ClearScreen();
  void MoveCursor(std::size_t row, std::size_t col);

  void SetResponseCallback(std::function<void(const std::string &)> callback) {
    m_responseCallback = callback;
  }
  void SetTitleCallback(std::function<void(const std::string &)> callback) {
    m_titleCallback = callback;
  }

  std::size_t Rows() const { return m_rows; }
  std::size_t Cols() const { return m_cols; }
  std::size_t MaxLines() const { return m_maxLines; }
  void SetMaxLines(std::size_t maxLines) { m_maxLines = maxLines; }

  // Cursor position relative to viewport
  wxPoint Cursor() const;

  // View into the buffer: returns rows [viewStart .. viewStart+m_rows)
  std::size_t ViewStart() const { return m_viewStart; }
  std::size_t ShellStart() const { return m_shellStart; }
  void SetViewStart(std::size_t vs);
  std::size_t TotalLines() const { return m_buffer.size(); }

  // Access a row by absolute index in the buffer
  const std::vector<Cell> &BufferRow(std::size_t absRow) const;

  // Returns the visible rows (view area) as a vector of pointers to rows
  std::vector<const std::vector<Cell> *> GetViewArea() const;

  std::string Flatten() const;

  // Convert viewport-relative row to absolute buffer row
  std::size_t AbsRow(std::size_t viewportRow) const;

  // Convert abs-row row to viewport row, return std::nullopt if it out of range
  std::optional<std::size_t> ViewPortRow(std::size_t absrow) const;

private:
  void PutChar(char c);
  void NewLine();
  void CarriageReturn();
  void Backspace();
  void Tab();
  void ScrollUp();
  void ParseEscape(const std::string &seq);
  void PutPrintable(char c);
  void PutPrintable(char32_t cp);
  void ApplySgr(const std::string &params);
  void PutString(const std::string &text);
  void HandleCsi(const std::string &seq);
  void PutCell(char c) { PutCell(static_cast<char32_t>(c)); }
  void PutCell(char32_t cp);

  // Scroll helpers (respect scroll region)
  void ScrollRegionUp();
  void ScrollRegionDown();

  std::size_t m_rows{24};
  std::size_t m_cols{80};
  std::size_t m_maxLines{10000};

  std::deque<std::vector<Cell>> m_buffer;
  std::size_t m_viewStart{0};
  std::size_t m_shellStart{0};
  wxPoint m_cursor{};

  // Scroll region (0-based, inclusive)
  std::size_t m_scrollTop{0};
  std::size_t m_scrollBottom{23}; // m_rows - 1

  // Saved cursor (for ESC[s / ESC[u)
  wxPoint m_savedCursor{};

  // Last printed character (for ESC[b repeat)
  char32_t m_lastChar{U' '};

  // Alternate screen buffer
  struct ScreenState {
    std::deque<std::vector<Cell>> buffer;
    std::size_t viewStart{0};
    std::size_t shellStart{0};
    wxPoint cursor{};
    std::size_t scrollTop{0};
    std::size_t scrollBottom{0};
  };
  ScreenState m_savedScreen;
  bool m_altScreenActive{false};

  bool m_inEscape{false};
  std::string m_escape;
  Cell m_attr{};
  wxTerminalTheme m_theme;
  std::function<void(const std::string &)> m_responseCallback;
  std::function<void(const std::string &)> m_titleCallback;
  std::string m_utf8Buf;
};

} // namespace terminal
