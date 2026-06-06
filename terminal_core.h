#pragma once

#include "terminal_theme.h"
#include "wx/colour.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <vector>
#include <wx/gdicmn.h>

extern "C" {
struct tsm_screen;
struct tsm_vte;
struct tsm_screen_attr;
typedef uint_fast32_t tsm_age_t;
}

namespace terminal {

template <typename EnumName>
inline bool IsFlagSet(EnumName flags, EnumName flag) {
  using T = std::underlying_type_t<EnumName>;
  return (static_cast<T>(flags) & static_cast<T>(flag)) == static_cast<T>(flag);
}

/// Checked subtraction: returns std::nullopt if a - b would overflow/underflow.
template <typename T> constexpr std::optional<T> CheckedSub(T a, T b) {
  if constexpr (std::is_unsigned_v<T>) {
    return (b > a) ? std::nullopt : std::optional<T>(a - b);
  } else {
    // Signed overflow check
    if (b > 0 && a < std::numeric_limits<T>::min() + b)
      return std::nullopt;
    if (b < 0 && a > std::numeric_limits<T>::max() + b)
      return std::nullopt;
    return a - b;
  }
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
  ~TerminalCore();

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
  void SetBellCallback(std::function<void()> callback) {
    m_bellCallback = callback;
  }

  std::size_t Rows() const { return m_rows; }
  std::size_t Cols() const { return m_cols; }
  std::size_t MaxLines() const { return m_maxLines; }
  void SetMaxLines(std::size_t maxLines);

  // Cursor position relative to viewport
  wxPoint Cursor() const;

  // View into the buffer: returns rows [viewStart .. viewStart+m_rows)
  std::size_t ViewStart() const { return m_viewStart; }
  std::size_t ShellStart() const;
  void SetViewStart(std::size_t vs);
  std::size_t TotalLines() const;

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

  void SetClickedRange(const wxRect &absRect);
  bool ClearClickedRange();
  wxString GetClickedText() const;
  wxString GetTextRange(std::size_t row, std::size_t col,
                        std::size_t count) const;

private:
  void DoSetClickedRange(bool b);
  void RefreshActiveScreen();
  void RefreshScrollbackCache();
  void SyncPalette();
  std::vector<Cell> ConvertScrollbackLine(unsigned int lineIdx) const;

  static void TsmWriteCb(struct tsm_vte *vte, const char *u8, size_t len,
                         void *data);
  static void TsmOscCb(struct tsm_vte *vte, const char *u8, size_t len,
                       void *data);
  static void TsmBellCb(struct tsm_vte *vte, void *data);
  static int TsmDrawCb(struct tsm_screen *con, uint64_t id, const uint32_t *ch,
                       size_t len, unsigned int width, unsigned int posx,
                       unsigned int posy, const struct tsm_screen_attr *attr,
                       tsm_age_t age, void *data);

  std::size_t m_rows{24};
  std::size_t m_cols{80};
  std::size_t m_maxLines{1000};

  // Active screen content (refreshed from tsm_screen_draw after each PutData)
  std::vector<std::vector<Cell>> m_activeScreen;

  // Cache for scrollback rows currently visible in the viewport.
  // Populated by RefreshScrollbackCache() when viewStart < ShellStart().
  std::vector<std::vector<Cell>> m_sbCache;
  std::size_t m_sbCacheStart{0};

  // Where the user is currently looking (changes on scroll wheel).
  std::size_t m_viewStart{0};
  // Whether the user is scrolled to the bottom (following output)
  bool m_followingBottom{true};

  // libtsm handles
  struct tsm_screen *m_tsmScreen{nullptr};
  struct tsm_vte *m_tsmVte{nullptr};

  wxTerminalTheme m_theme;
  std::function<void(const std::string &)> m_responseCallback;
  std::function<void(const std::string &)> m_titleCallback;
  std::function<void()> m_bellCallback;
  wxRect m_clickedRect;
};

} // namespace terminal
