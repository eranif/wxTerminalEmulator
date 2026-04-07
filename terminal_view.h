#pragma once

#include "terminal_logger.h"
#if defined(__WXGTK__) || defined(__WXMAC__)
#define USE_TIMER_REFRESH 0
#else
#define USE_TIMER_REFRESH 0
#endif

#include "pty_backend.h"
#include "terminal_core.h"
#if USE_TIMER_REFRESH
#include <thread>
#endif
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/panel.h>

#include <wx/dc.h>
#include <wx/timer.h>

#include <memory>
#include <optional>
#include <unordered_set>
#include <wx/arrstr.h>

class wxTerminalViewCtrl : public wxPanel {
public:
  using EnvironmentList = terminal::PtyBackend::EnvironmentList;

  wxTerminalViewCtrl(wxWindow *parent, const wxString &shellCommand,
                     const std::optional<EnvironmentList> &environment);
  ~wxTerminalViewCtrl() override;

  /**
   * @brief Sends input text to the terminal backend.
   *
   * In the context of TerminalView, this forwards the provided text to the
   * active backend if one is available. If no backend is attached, the call has
   * no effect.
   *
   * @param text const std::string& The input text to write to the terminal.
   *
   * @return void. This function does not return a value.
   */
  void SendInput(const std::string &text);
  void SetTerminalSizeFromClient();

  // Get the entire screen content
  wxString GetText() const;

  // Helper methods for sending special characters
  void SendEnter();
  void SendTab();
  void SendEscape();
  void SendBackspace();
  void SendArrowUp();
  void SendArrowDown();
  void SendArrowLeft();
  void SendArrowRight();
  void SendHome();
  void SendEnd();
  void SendDelete();
  void SendInsert();
  void SendPageUp();
  void SendPageDown();

  // Common keyboard shortcuts

  // Ctrl-C
  void SendCtrlC();
  // Clear screen
  void SendCtrlL();
  // Delete from cursor to start of line
  void SendCtrlU();
  // Delete from cursor to end of line
  void SendCtrlK();
  // Delete a word
  void SendCtrlW();
  // Suspend process
  void SendCtrlZ();
  // Trigger history search
  void SendCtrlR();
  // Logout
  void SendCtrlD();
  // Move cursor to beginning of line
  void SendCtrlA();
  // Move cursor to end of line
  void SendCtrlE();
  // Move backward 1 word
  void SendAltB();
  // Move forward 1 word
  void SendAltF();

  void Copy();
  void Paste();

  inline void EnableSafeDrawing(bool b) { m_safeDrawing = b; }
  inline bool IsSafeDrawing() const { return m_safeDrawing; }

  /**
   * @brief Clears the terminal screen and scrollback buffer for this
   * TerminalView.
   *
   * Sends the ANSI escape sequence to clear both the visible screen and the
   * scrollback history, then marks the view as needing a repaint.
   *
   * @param None.
   * @return void. This function does not return a value.
   */
  void ClearAll();

  void SetTheme(const wxTerminalTheme &theme);
  const wxTerminalTheme &GetTheme() const;

  void ScrollToLastLine();
  std::size_t GetLineCount() const;
  void SetBufferSize(std::size_t maxLines);
  std::size_t GetBufferSize() const;
  void CenterLine(std::size_t line);
  wxString GetLine(std::size_t line) const;
  wxString GetViewLine(std::size_t line) const;
  void SetUserSelection(std::size_t row, std::size_t col, std::size_t count);
  wxString
  GetRange(std::size_t row, std::size_t col,
           std::size_t count = std::numeric_limits<std::size_t>::max());
  void ClearUserSelection();
  void ClearMouseSelection();
  bool HasActiveSelection() const;
  void SetSelectionDelimChars(const wxString &delims);

  /**
   * @brief Converts a mouse position in client coordinates into a terminal
   * cell position.
   *
   * The returned point uses terminal cell coordinates, where x is the column
   * index and y is the row index. The input point must be relative to this
   * control's client area, not screen coordinates.
   *
   * @param pt wxPoint The mouse position in client coordinates.
   * @return wxPoint The corresponding cell coordinate (col, row).
   */
  std::optional<wxPoint> PointToCell(const wxPoint &pt) const;

  /**
   * @brief Sends a command string as input and submits it.
   *
   * @details This helper forwards the provided command to the input stream
   * using UTF-8 conversion, then simulates pressing Enter to execute or confirm
   * the command. It is intended for use in the enclosing class or component
   * that handles command entry.
   *
   * @param command const wxString& The command text to send.
   *
   * @return void This function does not return a value.
   */
  void SendCommand(const wxString &command) {
    SendInput(command.ToStdString(wxConvUTF8));
    SendEnter();
  }

  // Override to indicate this window can receive keyboard focus
  bool AcceptsFocus() const override { return true; }
  bool AcceptsFocusFromKeyboard() const override { return true; }

  // Override to prevent default navigation behavior for Enter/Tab/Escape
  bool ShouldInheritColours() const override { return true; }
  wxBorder GetDefaultBorder() const override { return wxBORDER_NONE; }

private:
  /// Linear selection defined by anchor and current cell in viewport
  /// coordinates. Cells between anchor and current in reading order
  /// (left-to-right, top-to-bottom) are considered selected.
  struct LinearSelection {
    wxPoint anchor;  // cell where mouse-down occurred
    wxPoint current; // cell where mouse currently is
    std::size_t viewStart{0}; // viewStart when selection was created
    bool active{false};

    void Clear() {
      active = false;
      anchor = current = {};
      viewStart = 0;
    }

    /// Return normalized start/end so start <= end in reading order.
    void GetNormalized(wxPoint &s, wxPoint &e) const {
      s = anchor;
      e = current;
      if (s.y > e.y || (s.y == e.y && s.x > e.x))
        std::swap(s, e);
    }

    bool Contains(int col, int row) const {
      if (!active)
        return false;
      wxPoint s, e;
      GetNormalized(s, e);
      if (row < s.y || row > e.y)
        return false;
      if (row == s.y && row == e.y)
        return col >= s.x && col <= e.x;
      if (row == s.y)
        return col >= s.x;
      if (row == e.y)
        return col <= e.x;
      return true; // middle row — fully selected
    }

    bool HasSelection() const {
      if (!active)
        return false;
      return anchor != current;
    }
  };
  void RefreshView(bool now = false);
  bool IsUnixKeyboardMode() const;
  bool IsPowerShell() const;
  bool IsCmdShell() const;
  bool IsShell(const wxString &shell_name, const wxArrayString &children) const;

  void Feed(const std::string &data);
  void StartProcess(const wxString &command,
                    const std::optional<EnvironmentList> &environment);

  wxColour GetColourFromTheme(std::optional<terminal::ColourSpec> spec,
                              bool foreground) const;
  void OnPaint(wxPaintEvent &evt);
  struct PaintCounters {
    PaintCounters(size_t &draw_text, size_t &draw_rectangle,
                  size_t &grouped_rows, size_t &full_row_draws)
        : draw_text_{draw_text}, draw_rectangle_{draw_rectangle},
          full_row_draws_{full_row_draws}, grouped_rows_{grouped_rows} {}
    size_t &draw_text_;
    size_t &draw_rectangle_;
    size_t &full_row_draws_;
    size_t &grouped_rows_;
  };

  void RenderRow(wxDC &dc, int y, int rowIdx,
                 const std::vector<terminal::Cell> &row,
                 PaintCounters &counters);
  void RenderRowNoGrouping(wxDC &dc, int y, int rowIdx,
                           const std::vector<terminal::Cell> &row,
                           PaintCounters &counters);
  void RenderRowWithGrouping(wxDC &dc, int y, int rowIdx,
                             const std::vector<terminal::Cell> &row,
                             PaintCounters &counters);
  void RenderRowPosix(wxDC &dc, int y, int rowIdx,
                      const std::vector<terminal::Cell> &row,
                      PaintCounters &counters);

  void OnSize(wxSizeEvent &evt);
  void OnCharHook(wxKeyEvent &evt);
  void OnKeyDown(wxKeyEvent &evt);
  void OnMouseLeftDown(wxMouseEvent &evt);
  void OnMouseLeftDoubleClick(wxMouseEvent &evt);
  void OnMouseMove(wxMouseEvent &evt);
  void OnMouseUp(wxMouseEvent &evt);
  void OnContextMenu(wxContextMenuEvent &evt);
  void OnMouseWheel(wxMouseEvent &evt);
  void OnFocus(wxFocusEvent &evt);
  void OnLostFocus(wxFocusEvent &evt);
  void OnCopy(wxCommandEvent &evt);
  void OnPaste(wxCommandEvent &evt);
  void OnClearBuffer(wxCommandEvent &evt);
  void DrawFocusBorder(wxDC &dc) const;
  void DebugDumpViewArea(TerminalLogLevel log_level, int viewLine = -1);
  void UpdateFontCache();
  const wxFont &GetCachedFont(bool bold, bool underlined) const;
  /**
   * @brief Computes the selection rectangle from mouse position.
   *
   * The rectangle represents a view cells.
   *
   * This method maps the given point to a terminal cell, then expands left and
   * right across the current row until a selection delimiter is reached. If the
   * point does not map to a valid cell, or the row/cell is out of bounds, no
   * selection rectangle is produced.
   *
   * @param pt const wxPoint& The mouse position in view coordinates (i.e. not
   * screen coordinates)
   * @param is_valid_char callback the be called on each character.
   *
   * @return std::optional<wxRect> The selection rectangle in view **cell**
   * coordinates, or std::nullopt if no valid selection can be determined.
   */
  std::optional<wxRect> SelectionRectFromMousePoint(
      const wxPoint &pt,
      std::function<bool(const wxUniChar &)> is_valid_char) const;

  /**
   * Handles terminal-specific special key events by translating supported
   * wxWidgets key codes into ANSI escape sequences and sending them to the
   * terminal input stream.
   *
   * This TerminalView method recognizes navigation keys, insert/delete keys,
   * page movement keys, and function keys F1 through F12. When a supported key
   * is detected, it calls SendInput() with the corresponding escape sequence
   * and reports that the event was handled; unsupported keys are ignored and
   * left for other handlers.
   *
   * @param key_event wxKeyEvent& The keyboard event containing the key code to
   * inspect and translate.
   * @return bool Returns true if the key was recognized and an escape sequence
   * was sent; returns false if the key is not handled by this method.
   */
  bool HandleSpecialKeys(wxKeyEvent &key_event);

  void DoClickable(wxMouseEvent &event);

  struct ApiSelection {
    std::size_t row{0}, col{0}, endCol{0};
    bool active{false};
    inline void Clear() {
      row = col = endCol = 0;
      active = false;
    }
  };

  struct CellAttributes {
    wxColour fgColor;
    wxColour bgColor;
    bool bold;
    bool underline;
    bool isMouseSelected;
    bool isApiSelected;
    bool isClicked{false};

    bool operator==(const CellAttributes &other) const {
      return fgColor == other.fgColor && bgColor == other.bgColor &&
             bold == other.bold && underline == other.underline &&
             isMouseSelected == other.isMouseSelected &&
             isClicked == other.isClicked &&
             isApiSelected == other.isApiSelected;
    }

    bool operator!=(const CellAttributes &other) const {
      return !(*this == other);
    }
  };

  struct CellInfo {
    int colIdx{wxNOT_FOUND};
    wxChar ch{' '};
    CellAttributes attrs;
    inline bool IsUnicode() const { return ch >= 0x80; }
    inline bool HasSameAttributes(const CellInfo &other) const {
      return attrs == other.attrs;
    }
    inline bool IsAdjacent(const CellInfo &other) const {
      return IsLeftTo(other) || IsRightTo(other);
    }
    inline bool IsLeftTo(const CellInfo &other) const {
      return colIdx == other.colIdx - 1;
    }
    inline bool IsRightTo(const CellInfo &other) const {
      return colIdx == other.colIdx + 1;
    }
    inline bool IsSelected() const {
      return attrs.isMouseSelected || attrs.isApiSelected;
    }
    inline bool IsOk() const { return colIdx != wxNOT_FOUND; }
  };

  void PrepareDcForTextDrawing(wxDC &dc,
                               const wxTerminalViewCtrl::CellInfo &cell);

  /**
   * @brief Checks whether a cell can be safely grouped with neighboring cells
   * for text rendering.
   *
   * This accepts printable ASCII and a conservative subset of Unicode code
   * points that are expected to occupy exactly one terminal cell and do not
   * require shaping across adjacent cells.
   */
  bool IsUnicodeSingleCellSafe(wxChar ch) const;

  struct PrepareRowForDrawingResult {
    std::vector<wxTerminalViewCtrl::CellInfo> cells;
    bool is_ascii_safe{true};
    bool row_has_selection{false};
  };

  PrepareRowForDrawingResult
  PrepareRowForDrawing(const std::vector<terminal::Cell> &row, int rowIdx);
  /// Draw a row where all of its cells share the same attributes
  void
  RenderMonotonicRow(wxDC &dc, int y, int rowIdx,
                     const std::vector<wxTerminalViewCtrl::CellInfo> &cells,
                     PaintCounters &counters);

  terminal::TerminalCore m_core;
  std::unique_ptr<terminal::PtyBackend> m_backend;
  LinearSelection m_mouseSelection{};
  ApiSelection m_userSelection;
  bool m_isDragging{false};
  int m_scrollOffset{0}; // 0 = at bottom, >0 = scrolled back
  int m_wheelAccum{0};

  wxFont m_defaultFont;
  wxFont m_defaultFontBold;
  wxFont m_defaultFontUnderlined;
  wxFont m_defaultFontBoldUnderlined;
  int m_charW{0};
  int m_charH{0};
  bool m_contextMenuShowing{false};
  wxString m_shell_command;
  std::optional<EnvironmentList> m_environment{std::nullopt};
  bool m_safeDrawing{false};
#if USE_TIMER_REFRESH
  std::atomic_bool m_needsRepaint{true};
  std::unique_ptr<std::thread> m_drawingTimerThread{nullptr};
  std::atomic_bool m_shutdownFlag{false};
#endif
  std::unordered_set<wxChar> m_selectionDelimChars;
  bool m_hasFocusBorder{false};
};
