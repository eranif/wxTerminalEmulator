#pragma once

#include "terminal_theme.h"
#include "wx/colour.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <wx/gdicmn.h>

namespace terminal {

template <typename EnumName>
inline bool IsFlagSet(EnumName flags, EnumName flag) {
  using T = std::underlying_type_t<EnumName>;
  return (static_cast<T>(flags) & static_cast<T>(flag)) == static_cast<T>(flag);
}

template <typename EnumName>
inline void AddFlagSet(EnumName &flags, EnumName flag) {
  using T = std::underlying_type_t<EnumName>;
  T &t = reinterpret_cast<T &>(flags);
  t |= static_cast<T>(flag);
}

template <typename EnumName>
inline void RemoveFlag(EnumName &flags, EnumName flag) {
  using T = std::underlying_type_t<EnumName>;
  T &t = reinterpret_cast<T &>(flags);
  t &= ~static_cast<T>(flag);
}

template <typename EnumName>
inline void SetFlag(EnumName &flags, EnumName flag, bool b) {
  if (b) {
    AddFlagSet(flags, flag);
  } else {
    RemoveFlag(flags, flag);
  }
}

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

enum class CellFlags {
  kNone = 0,
  kBold = (1 << 0),
  kUnderlined = (1 << 1),
  kReverse = (1 << 2),
  kClicked = (1 << 3),
};

struct Cell {
  char32_t ch{U' '};
  std::optional<CellColours> colours{std::nullopt};
  CellFlags flags{CellFlags::kNone};

  inline void SetBold(bool b) { SetFlag(flags, CellFlags::kBold, b); }
  inline bool IsBold() const { return IsFlagSet(flags, CellFlags::kBold); }
  inline void SetUnderlined(bool b) {
    SetFlag(flags, CellFlags::kUnderlined, b);
  }
  inline bool IsUnderlined() const {
    return IsFlagSet(flags, CellFlags::kUnderlined);
  }
  inline void SetReverse(bool b) { SetFlag(flags, CellFlags::kReverse, b); }
  inline bool IsReverse() const {
    return IsFlagSet(flags, CellFlags::kReverse);
  }
  inline void SetClicked(bool b) { SetFlag(flags, CellFlags::kClicked, b); }
  inline bool IsClicked() const {
    return IsFlagSet(flags, CellFlags::kClicked);
  }
  inline bool IsEmpty() const { return ch == U' ' && !colours.has_value(); }
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
  inline static Cell New(char32_t c = U' ') {
    Cell cell;
    cell.ch = c;
    return cell;
  }
};

class TerminalCore {
public:
  TerminalCore(std::size_t rows = 24, std::size_t cols = 80,
               std::size_t maxLines = 1000);

  void SetTheme(const wxTerminalTheme &theme);
  const wxTerminalTheme &GetTheme() const { return m_theme; }
  wxTerminalTheme &GetTheme() { return m_theme; }

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
  void SetBellCallback(std::function<void()> callback) { m_bellCallback = callback; }

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

  // Access a row by its view index
  const std::vector<Cell> &ViewBufferRow(std::size_t absRow) const;

  // Returns the visible rows (view area) as a vector of pointers to rows
  std::vector<const std::vector<Cell> *> GetViewArea() const;

  wxString Flatten() const;

  // Convert viewport-relative row to absolute buffer row
  std::size_t AbsRow(std::size_t viewportRow) const;

  // Convert abs-row row to viewport row, return std::nullopt if it out of range
  std::optional<std::size_t> ViewPortRow(std::size_t absrow) const;
  Cell *GetCell(const wxPoint &absCoords);

  /**
   * @brief Sets the clicked range for this TerminalCore instance.
   *
   * Updates the stored absolute clicked rectangle and refreshes the internal
   * clicked-range state before and after the assignment.
   *
   * @param absRect const wxRect& The absolute rectangle to store as the clicked
   * range.
   *
   * @return void
   */
  void SetClickedRange(const wxRect &absRect);

  /**
   * @brief Clears the current clicked range, if any, from the terminal core.
   *
   * This method belongs to TerminalCore and resets the stored clicked rectangle
   * only when a non-empty range is currently selected.
   *
   * @return bool True if a clicked range was present and was cleared; false if
   * there was no clicked range to clear.
   */
  bool ClearClickedRange();
  /**
   * @brief Returns the text currently associated with the clicked rectangle.
   *
   * This TerminalCore method uses the stored click geometry to extract the text
   * range at the clicked location. If no clicked rectangle is set, it returns
   * an empty string without performing any text lookup.
   *
   * @return wxString The text extracted from the clicked rectangle, or an empty
   *         string if no clicked rectangle is available.
   */
  wxString GetClickedText() const;
  wxString GetTextRange(std::size_t row, std::size_t col,
                        std::size_t count) const;

private:
  void DoSetClickedRange(bool b);
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
  void PutCell(char c) { PutCell(static_cast<char32_t>(c)); }
  void PutCell(char32_t cp);

  // Scroll helpers (respect scroll region)
  void ScrollRegionUp();
  void ScrollRegionDown();

  std::size_t m_rows{24};
  std::size_t m_cols{80};
  std::size_t m_maxLines{1000};

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
  std::function<void()> m_bellCallback;
  std::string m_utf8Buf;
  wxRect m_clickedRect;
};

} // namespace terminal
