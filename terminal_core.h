#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace terminal {

struct Cell {
  char32_t ch{U' '};
  std::uint32_t fg{0x00C0C0C0};
  std::uint32_t bg{0x00000000};
  bool bold{false};
  bool underline{false};
  bool reverse{false};
};

struct CursorPos {
  std::size_t row{0};
  std::size_t col{0};
};

class TerminalCore {
public:
  TerminalCore(std::size_t rows = 24, std::size_t cols = 80, std::size_t scrollback = 2000);

  void Resize(std::size_t rows, std::size_t cols);
  void Reset();
  void PutData(const std::string& data);
  void SetViewportSize(std::size_t rows, std::size_t cols);
  void AppendLine(const std::string& line);
  void ClearScreen();
  void MoveCursor(std::size_t row, std::size_t col);

  std::size_t Rows() const { return m_rows; }
  std::size_t Cols() const { return m_cols; }
  CursorPos Cursor() const { return m_cursor; }

  const std::vector<std::vector<Cell>>& Screen() const { return m_screen; }
  const std::vector<std::string>& Scrollback() const { return m_scrollback; }

  std::string Flatten() const;

private:
  void PutChar(char c);
  void NewLine();
  void CarriageReturn();
  void Backspace();
  void Tab();
  void ScrollUp();
  void ParseEscape(const std::string& seq);
  void PutPrintable(char c);
  void ApplySgr(const std::string& params);
  void PutString(const std::string& text);
  void HandleCsi(const std::string& seq);
  void PutCell(char c);

  std::size_t m_rows{24};
  std::size_t m_cols{80};
  std::size_t m_scrollbackLimit{2000};
  std::vector<std::vector<Cell>> m_screen;
  std::vector<std::string> m_scrollback;
  CursorPos m_cursor{};
  bool m_inEscape{false};
  std::string m_escape;
  Cell m_attr{};
};

} // namespace terminal
