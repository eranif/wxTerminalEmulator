#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
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
  TerminalCore(std::size_t rows = 24, std::size_t cols = 80,
               std::size_t maxLines = 10000);

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

  // Cursor position relative to viewport
  CursorPos Cursor() const;

  // View into the buffer: returns rows [viewStart .. viewStart+m_rows)
  std::size_t ViewStart() const { return m_viewStart; }
  void SetViewStart(std::size_t vs);
  std::size_t TotalLines() const { return m_buffer.size(); }

  // Access a row by absolute index in the buffer
  const std::vector<Cell> &BufferRow(std::size_t absRow) const;

  std::string Flatten() const;

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
  void PutCell(char c);
  void PutCell(char32_t cp);

  // Convert viewport-relative row to absolute buffer row
  std::size_t AbsRow(std::size_t viewportRow) const;

  std::size_t m_rows{24};
  std::size_t m_cols{80};
  std::size_t m_maxLines{10000};

  std::deque<std::vector<Cell>> m_buffer;
  std::size_t m_viewStart{0}; // First visible row in buffer
  CursorPos m_cursor{};       // Relative to viewport

  bool m_inEscape{false};
  std::string m_escape;
  Cell m_attr{};
  std::function<void(const std::string &)> m_responseCallback;
  std::function<void(const std::string &)> m_titleCallback;
  std::string m_utf8Buf;
};

} // namespace terminal
