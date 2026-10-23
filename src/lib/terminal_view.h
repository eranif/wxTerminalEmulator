#pragma once

#include "terminal_logger.h"
#if USE_OPENGL
#define USE_TIMER_REFRESH 0
#else
#define USE_TIMER_REFRESH 0
#endif

#include "pty_backend.h"
#include "terminal_core.h"
#include "terminal_gauge.h"
#include "terminal_gl_renderer.h"
#if USE_TIMER_REFRESH
#include <thread>
#endif
#include <atomic>
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/panel.h>
#if USE_OPENGL
#include <memory>
#include <wx/glcanvas.h>
#endif

#include <wx/dc.h>
#include <wx/timer.h>

#include <memory>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <wx/arrstr.h>
#include <wx/sizer.h>
#include <wx/weakref.h>
#include <wx/wupdlock.h>

// When the OpenGL path is enabled the control is a wxGLCanvas (still a
// wxWindow, so all event binding, scrollbars, focus, etc. behave the same);
// otherwise it remains a plain wxPanel using the legacy wxDC renderer.
#if USE_OPENGL
using wxTerminalViewBase = wxGLCanvas;
#else
using wxTerminalViewBase = wxPanel;
#endif

class wxTerminalViewCtrl : public wxTerminalViewBase {
public:
  using EnvironmentList = terminal::PtyBackend::EnvironmentList;

  /**
   * @param shellIntegration When true (the default), the control adds shell
   *        hooks to the environment of the child process so that bash reports
   *        its directory (OSC 7 / OSC 9;9, see wxEVT_TERMINAL_CWD_CHANGED),
   *        also over ssh and in WSL, and cmd.exe does the same on Windows. The
   *        hooks are added to `environment` when it is given, otherwise to a
   *        copy of the process environment. An existing PROMPT_COMMAND (or
   *        PROMPT on Windows) is kept and extended. When a working directory
   *        is given, CHERE_INVOKING=1 is set too, so an MSYS2 login shell stays
   *        in that directory. Set to false to leave the
   *        environment untouched.
   */
  wxTerminalViewCtrl(wxWindow *parent, const wxString &shellCommand,
                     const std::optional<EnvironmentList> &environment,
                     std::optional<wxString> workingDirectory = std::nullopt,
                     bool showScrollBar = true, bool shellIntegration = true);
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

  /// Get the terminal text (scrollback lines included).
  /// @param from Absolute index of the first line to return.
  /// @param count Maximum number of lines to return.
  /// Both are clamped to the buffer; an out of range `from` yields an empty
  /// string. With the defaults the entire text is returned.
  wxString
  GetText(std::size_t from = 0,
          std::size_t count = std::numeric_limits<std::size_t>::max()) const;

  /// Absolute index of the first line currently visible on the screen.
  std::size_t GetFirstVisibleLine() const;
  /// Number of lines that can be drawn onto the screen.
  std::size_t GetLinesOnScreen() const;
  /// Absolute index of the last line in the buffer.
  std::size_t GetLastLine() const;

  // Launch parameters this view was created with. Used to serialize/restore
  // the terminal layout so the same shells can be recreated on startup.
  const wxString &GetShellCommand() const { return m_shell_command; }
  /// True while the program in the terminal tracks the mouse (tmux, vim, ...).
  /// See wxEVT_TERMINAL_APP_OWNS_MOUSE.
  bool IsMouseTracking() const { return m_core.IsMouseTracking(); }
  wxString GetWorkingDirectory() const {
    return m_workingDirectory ? wxString::FromUTF8(m_workingDirectory->c_str())
                              : wxString{};
  }

  /// Last directory the program reported with OSC 7 or OSC 9;9, or an empty
  /// string.
  wxString GetCurrentDir() const { return m_currentDirectory; }

  /// Progress the program reported with OSC 9;4. Value is 0-100.
  wxTerminalProgressState GetProgressState() const { return m_progressState; }
  int GetProgressValue() const { return m_progressValue; }

  /// Latest OSC 7501 report of the root record (a report without an id). The
  /// state is Clear when there is none, or when the program has ended.
  const wxTerminalProgramStatus &GetProgramStatus() const {
    return m_programStatus;
  }

  static bool IsOpenGLEnabled();

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

  // Transmit a cursor-style key, honouring DECCKM (CSI vs SS3 encoding).
  void SendCursorKey(char final);

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
  bool CanCopy() const { return HasActiveSelection(); }
  void Paste();
  void PasteFromPrimarySelection();
  // Return the currently selected text or nullopt if there is no selection.
  std::optional<wxString> GetMouseSelection() const;
  // Return the text that the running program last copied with OSC 52 (for
  // example tmux or vim, which select text themselves while they track the
  // mouse), or nullopt if there is none. It is set when the user copies in the
  // program, not while they select. The text is only stored (and announced by
  // wxEVT_TERMINAL_APP_SELECTION_CHANGED): the system clipboard is not
  // written, so the application decides what to do with it.
  std::optional<wxString> GetAppSelection() const;
  // Forget the text returned by GetAppSelection(). It is also cleared on a left
  // press or double click in the terminal, when the program stops tracking the
  // mouse, and by ClearAll(). Fires wxEVT_TERMINAL_APP_SELECTION_CHANGED (empty
  // string) if there was text.
  void ClearAppSelection();

  enum SearchFlags {
    kForward = 1 << 0,
    kBackward = 1 << 1,
    kCaseInSensitive = 1 << 2,
  };

  void FindText(const wxString &text, size_t flags = SearchFlags::kForward);
  bool HasActiveSearch() const;

  inline void EnableSafeDrawing(bool b) {
    m_safeDrawing = b;
    InvalidateRenderCache();
  }
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
    if (m_backendReady) {
      SendInput(command.ToStdString(wxConvUTF8));
      SendEnter();
      return;
    }
    // queue these commands until the backend is ready
    m_pendingCommands.push_back(command);
  }

  using OutputCallback = std::function<void(const std::string &)>;

  /// Set a callback to be invoked with each chunk of raw output received from
  /// the terminal process. Useful for detecting prompts or waiting-for-input
  /// patterns. Pass nullptr or an empty function to remove.
  void SetOutputCallback(OutputCallback callback) {
    m_outputCallback = std::move(callback);
  }

  /// Key event filter: lets the owner of the control handle keyboard
  /// shortcuts before the terminal does. Return true to consume the key (the
  /// terminal ignores it), false to let the terminal process it as usual.
  using KeyEventFilter = std::function<bool(const wxKeyEvent &)>;

  /// Register a filter that is called for every "special" key press: function
  /// keys, navigation keys, Enter/Tab/Escape/Backspace and any key combined
  /// with Ctrl, Alt or Meta (Ctrl+<Letter>, Ctrl+Alt+<Letter>, ...). It is
  /// not called for a pure character (a printable key with no modifier other
  /// than Shift) nor for a modifier key pressed on its own. Pass nullptr to
  /// remove the filter.
  void RegisterKeyEventFilter(KeyEventFilter cb) {
    m_keyEventFilter = std::move(cb);
  }

  /// A callback to be called whenever the user Ctrl/Cmd+Left-CLICK on a text in
  /// the terminal. If the callback returns "true", the caller takes the
  /// ownership on the event and the app running inside the terminal sees
  /// nothing.
  using TextClickedFilter = std::function<bool(const std::string &)>;
  void RegisterTextClickedFilter(TextClickedFilter cb) {
    m_textClickedFilter = std::move(cb);
  }

  // Override to indicate this window can receive keyboard focus
  bool AcceptsFocus() const override { return true; }
  bool AcceptsFocusFromKeyboard() const override { return true; }

  // Override to prevent default navigation behavior for Enter/Tab/Escape
  bool ShouldInheritColours() const override { return true; }
  wxBorder GetDefaultBorder() const override { return wxBORDER_NONE; }

  // Call this method to ensure that the terminal is started even if the
  // terminal is placed in a hidden window.
  inline void EnsureStarted() { InitialiseAndStart(nullptr); }

  /// Reserve a strip at the top of the view and show `text` in it. The strip is
  /// one text row high, followed by a thin separator line. It is not part of
  /// the terminal: it is always visible (it never scrolls), the program in the
  /// terminal does not see its rows (the terminal gets that many fewer rows),
  /// and the mouse does not select or report anything over it. Calling it
  /// again replaces the text.
  ///
  /// @param fg text colour; the default is the foreground of the theme.
  /// @param bg background colour of the strip; the default is the theme
  /// background, slightly lightened (or darkened) towards the foreground.
  /// @param font font of the text; the default is the terminal font. With
  /// another font the strip is as high as that font, and the text is laid out
  /// with the width of its characters, not on the cell grid.
  void AddHeader(const wxString &text, const wxColour &fg = wxNullColour,
                 const wxColour &bg = wxNullColour,
                 const wxFont &font = wxNullFont);
  /// Remove the strip added by AddHeader() and give its rows back to the
  /// terminal.
  void RemoveHeader();
  bool HasHeader() const { return m_header.present; }
  const wxString &GetHeaderText() const { return m_header.text; }

  /// Like AddHeader(), for a strip at the bottom of the view. The separator
  /// line is above the text. A view can have a header and a footer at the same
  /// time.
  void AddFooter(const wxString &text, const wxColour &fg = wxNullColour,
                 const wxColour &bg = wxNullColour,
                 const wxFont &font = wxNullFont);
  /// Remove the strip added by AddFooter() and give its rows back to the
  /// terminal.
  void RemoveFooter();
  bool HasFooter() const { return m_footer.present; }
  const wxString &GetFooterText() const { return m_footer.text; }

  /// Show a thin progress bar above the view. It is a Gauge: the native
  /// wxGauge, or, when OpenGL is used (USE_OPENGL), a gauge drawn with OpenGL
  /// (see terminal_gauge.h). It shows the progress the program reports with
  /// OSC 9;4, or else with OSC 7501 (a working or blocked program): a bar with
  /// a percent, or a moving bar when there is no percent. The native gauge has
  /// the colour of the system; the OpenGL gauge is green (see
  /// SetProgressColour() and SetProgressBackgroundColour()). An error or a
  /// warning does not change the colour.
  ///
  /// The gauge is a child of the parent window, inserted at the start of the
  /// sizer of the parent: call this after the parent has its sizer, which is
  /// normally vertical. Nothing happens, and IsShowProgressBar() stays false, when the
  /// parent has no sizer, or when the parent is a wxBookCtrlBase (a notebook:
  /// its pages share the parent).
  ///
  /// The gauge is created once, the first time the bar is shown. After that
  /// it is only shown or hidden: it is shown while there is progress (and the
  /// view is shown), and hidden otherwise, when it takes no room in the sizer.
  /// The terminal is a few pixels smaller while the gauge is shown, so its
  /// number of rows changes only if that crosses a row boundary. The gauge is
  /// destroyed with the view. The bar is off by default.
  void ShowProgressBar(bool show);
  /// True if the progress bar was asked for and could be created.
  bool IsShowProgressBar() const { return m_showProgressBar; }

  /// The colour of the progress bar. The default (also after passing
  /// wxNullColour) is the green of the theme, and it follows the theme. Only
  /// the OpenGL gauge has colours of its own: the native wxGauge keeps the
  /// colours of the system. They can be set before the bar is shown.
  void SetProgressColour(const wxColour &colour);
  /// The colour the bar uses now: the one that was set, else the green of the
  /// theme. It is never invalid.
  wxColour GetProgressColour() const;

  /// The background of the progress bar, behind the bar. The default (also
  /// after passing wxNullColour) is the background colour of the theme, and it
  /// follows the theme. The colours are used as they are: nothing is adjusted
  /// for contrast.
  void SetProgressBackgroundColour(const wxColour &colour);
  /// The colour the background uses now: the one that was set, else the
  /// background colour of the theme. It is never invalid.
  wxColour GetProgressBackgroundColour() const;

private:
  /// The text of a header or footer laid out in pixels (the same layout for
  /// both renderers): one entry per visible character, on the cell grid when
  /// the strip uses the terminal font.
  struct StripGlyph {
    char32_t ch;
    int x;     ///< left edge in pixels
    int w;     ///< width in pixels
    bool wide; ///< covers more than one terminal column (an emoji, CJK)
  };
  /// A strip of text above (header) or below (footer) the terminal grid.
  struct Strip {
    bool present{false};
    wxString text;
    wxColour fg;       // invalid: use the theme
    wxColour bg;       // invalid: derive from the theme
    wxFont font;       // invalid: use the terminal font
    int fontHeight{0}; // line height of `font`, in pixels
    // Cache of LayoutStrip().
    mutable std::vector<StripGlyph> layout;
    mutable bool layoutValid{false};
    mutable int layoutWidth{0};
    mutable int layoutCharW{0};
  };
  /// Where the parts of a strip are, in client coordinates.
  struct StripRects {
    wxRect text;
    wxRect separator;
  };

  /// Height in pixels of the text row of a strip (without the separator).
  int StripTextHeight(const Strip &strip) const;
  /// Height in pixels of a strip with its separator, 0 if there is none (or the
  /// font is not measured yet).
  int StripHeight(const Strip &strip) const;
  int HeaderHeight() const { return StripHeight(m_header); }
  int FooterHeight() const { return StripHeight(m_footer); }
  /// The progress to show: OSC 9;4 first, else OSC 7501. The state is Hidden
  /// when there is nothing to show; the value is 0-100.
  void GetEffectiveProgress(wxTerminalProgressState &state, int &value) const;
  /// Update the gauge after the progress changed.
  void OnProgressChanged();
  /// Give the OpenGL gauge the colours of the theme (the native one has none).
  void ApplyProgressBarColours();
  /// Show the gauge only while the bar is enabled, the view is shown and there
  /// is progress to show.
  void UpdateProgressBarVisibility(bool viewShown);
  void OnShow(wxShowEvent &event);
  /// Keeps an indeterminate gauge moving.
  void OnProgressTimer(wxTimerEvent &event);
  /// Remove the gauge from the sizer of the parent and destroy it. Only the
  /// destructor does this: the gauge belongs to the parent, which can outlive
  /// the view.
  void DestroyProgressBar();
  /// Set or remove a strip, then re-layout the terminal and repaint.
  void SetStrip(Strip &strip, const wxString &text, const wxColour &fg,
                const wxColour &bg, const wxFont &font);
  void ClearStrip(Strip &strip);
  void OnStripChanged();
  const std::vector<StripGlyph> &LayoutStrip(const Strip &strip) const;
  /// The effective colours of a strip: its own or derived from the theme.
  void GetStripColours(const Strip &strip, wxColour &bg, wxColour &fg,
                       wxColour &separator) const;
  StripRects GetStripRects(const Strip &strip, bool footer) const;
  void DrawStrip(wxDC &dc, const Strip &strip, bool footer);
#if USE_OPENGL
  void DrawStripGL(const Strip &strip, bool footer);
#endif

  wxRect GetTerminalRect() const;
  wxSize GetTerminalSize() const;
  std::optional<wxString> MouseSelectionToString() const;

  /// Linear selection defined by anchor and current cell in viewport
  /// coordinates. Cells between anchor and current in reading order
  /// (left-to-right, top-to-bottom) are considered selected.
  struct LinearSelection {
    wxPoint anchor;           // cell where mouse-down occurred
    wxPoint current;          // cell where mouse currently is
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

    /// Return normalized start/end as absolute buffer rows.
    void GetAbsNormalized(wxPoint &s, wxPoint &e) const {
      GetNormalized(s, e);
      int vs = static_cast<int>(viewStart);
      s.y = std::max(s.y + vs, 0);
      e.y = std::max(e.y + vs, 0);
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

    bool HasSelection() const { return active; }

    /// Scroll the viewport by `delta` lines (negative=up, positive=down)
    /// and adjust the anchor so the selection stays consistent.
    void AdjustForScroll(int delta) { anchor.y -= delta; }
  };
  void RefreshView(bool now = false);
  bool IsUnixKeyboardMode() const;
  bool IsPowerShell() const;
  bool IsCmdShell() const;
  bool IsShell(const wxString &shell_name, const wxArrayString &children) const;
  bool InitialiseAndStart(wxDC *pdc = nullptr);

  void Feed(const std::string &data);
  void StartProcess(const wxString &command,
                    const std::optional<EnvironmentList> &environment);

  wxColour GetColourFromTheme(std::optional<terminal::ColourSpec> spec,
                              bool foreground) const;
  void OnPaint(wxPaintEvent &evt);
#if USE_OPENGL
  // GPU paint path: renders the visible grid with the glyph-atlas renderer.
  void OnPaintGL();
  // Emit the geometry for a single screen row into the GL renderer.
  void RenderRowGL(int y, int rowIdx, const std::vector<terminal::Cell> &row,
                   const wxColour &clearBg);
  void PaintResizeOverlayGL();
#endif
  // Legacy wxDC paint path (backing-store incremental renderer).
  void OnPaintDC();
  void PaintResizeOverlay(wxDC &dc);
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

  void OnSize(wxSizeEvent &evt);
  bool TopLevelWindowResized() const;
  void OnCharHook(wxKeyEvent &evt);
  void OnKeyDown(wxKeyEvent &evt);
  /// Send characters that wxEVT_KEY_DOWN can not report, i.e. characters of a
  /// non-Latin keyboard layout. Only code points above ASCII are handled here.
  void OnChar(wxKeyEvent &evt);
  void OnMouseLeftDown(wxMouseEvent &evt);
  void OnMouseLeftDoubleClick(wxMouseEvent &evt);
  void OnMouseMove(wxMouseEvent &evt);
  void OnMouseUp(wxMouseEvent &evt);
  /// Right/middle button events: report to libtsm and skip.
  void OnMouse(wxMouseEvent &evt);
  void OnMouseRightDown(wxMouseEvent &evt);
  /// True if the application enabled mouse tracking and wants this event.
  /// Shift bypasses the application, so the user can still select text.
  bool AppOwnsMouse(const wxMouseEvent &evt) const;
  /// Clamp a pixel position to the cell grid, so a position over the margin or
  /// outside the window is reported as the nearest cell.
  wxPoint ClampToGrid(const wxPoint &pos) const;
  /// Forward a mouse event to libtsm if the application owns this mouse
  /// gesture. Returns true if it does, so the caller must not handle the event
  /// itself. The decision is made at the button press and kept until the
  /// release.
  bool ReportMouse(const wxMouseEvent &evt);
  /// Write one mouse event to the application, at the cell and pixel of `evt`.
  void SendMouseToApp(const wxMouseEvent &evt, unsigned button, unsigned type);
  /// Release the mouse capture when no application-owned button is held and
  /// no local selection drag runs.
  void ReleaseCaptureIfIdle();
  /// Start a press the application owns: no local selection, and the mouse is
  /// captured so the release arrives even outside the window.
  void BeginAppOwnedPress();
  void OnMouseCaptureLost(wxMouseCaptureLostEvent &evt);
  void OnContextMenu(wxContextMenuEvent &evt);
  void OnMouseWheel(wxMouseEvent &evt);
  void OnFocus(wxFocusEvent &evt);
  void OnLostFocus(wxFocusEvent &evt);
  void OnCopy(wxCommandEvent &evt);
  void OnPaste(wxCommandEvent &evt);
  void OnMiddleClickPaste(wxMouseEvent &evt);
  void OnClearBuffer(wxCommandEvent &evt);
  void DrawFocusBorder(wxDC &dc) const;
  void DebugDumpViewArea(TerminalLogLevel log_level, int viewLine = -1);
  void UpdateFontCache();
  void UpdateScrollbar();
  void OnScroll(wxScrollWinEvent &evt);
  void OnEraseBg(wxEraseEvent &) {}
  void OnLeaveWindow(wxMouseEvent &e) { e.Skip(); }

  const wxFont &GetCachedFont(bool bold, bool underlined) const;
  /// True if `ch` (an emoji or a symbol) is drawn with the fallback font.
  bool UseFallbackFont(char32_t ch) const;

  /// The cell under the block cursor and the columns it covers.
  struct CursorCellInfo {
    const terminal::Cell *cell{nullptr};
    int col{0};  ///< first column of the character (its owner for a filler)
    int cols{1}; ///< columns covered: the width of a wide character, else 1
  };
  /// Find the cell at column `x` of `row`. A cursor on the filler cell of a
  /// wide character snaps to the wide cell that owns it.
  static CursorCellInfo CursorCellAt(const std::vector<terminal::Cell> &row,
                                     int x);

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

  /// Scroll viewport by `delta` lines (-1=up, +1=down) during selection,
  /// adjusting the anchor to keep the selection consistent.
  /// Returns true if scrolling actually occurred.
  bool ScrollViewportForSelection(int delta);

  void DoClickable(wxMouseEvent &event, bool fire_event);
  /// Set the mouse cursor: an arrow while the program tracks the mouse, else
  /// the I-beam.
  void UpdateCursor();

  /// The word under the mouse as a rect in **buffer** coordinates (the row
  /// includes the scrollback offset), or nullopt if there is no text there.
  std::optional<wxRect> WordRectFromMousePoint(const wxPoint &pt) const;
  /// The trimmed word under the mouse, or nullopt if there is none.
  std::optional<wxString> GetTextFromMousePoint(const wxPoint &pt) const;

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
    char32_t ch{U' '};
    unsigned int cellWidth{
        1}; // terminal columns spanned (2 for wide chars like emoji)
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

    // Used by the incremental renderer to detect whether a screen row's
    // visual content changed since the last paint. Two cells render
    // identically iff their column, glyph, width and attributes are equal.
    inline bool operator==(const CellInfo &other) const {
      return colIdx == other.colIdx && ch == other.ch &&
             cellWidth == other.cellWidth && attrs == other.attrs;
    }
    inline bool operator!=(const CellInfo &other) const {
      return !(*this == other);
    }
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
  bool IsUnicodeSingleCellSafe(char32_t ch) const;

  struct PrepareRowForDrawingResult {
    std::vector<wxTerminalViewCtrl::CellInfo> cells;
    bool is_ascii_safe{true};
    bool row_has_selection{false};
    /// True when the row has a cell that does not span exactly one column (a
    /// wide character such as CJK or an emoji, or its filler). Such rows must
    /// be drawn on the cell grid, never as a run of text.
    bool row_has_wide{false};
  };

  PrepareRowForDrawingResult
  PrepareRowForDrawing(const std::vector<terminal::Cell> &row, int rowIdx);
  /// Draw a row where all of its cells share the same attributes

  /// RAII helper: scrolls to the last line on destruction unless cancelled.
  struct EndLineScroller {
    wxTerminalViewCtrl *m_view{nullptr};
    explicit EndLineScroller(wxTerminalViewCtrl *view) : m_view(view) {}
    void Cancel() { m_view = nullptr; }
    ~EndLineScroller() {
      if (m_view)
        m_view->ScrollToLastLine();
    }
  };

  void
  RenderMonotonicRow(wxDC &dc, int y, int rowIdx,
                     const std::vector<wxTerminalViewCtrl::CellInfo> &cells,
                     PaintCounters &counters);

  terminal::TerminalCore m_core;
  std::unique_ptr<terminal::PtyBackend> m_backend;
  LinearSelection m_mouseSelection{};
  // The text the program last copied with OSC 52.
  std::optional<wxString> m_appSelection;
  ApiSelection m_userSelection;
  bool m_isDragging{false};
  // Mouse buttons (bit 1 left, 2 middle, 4 right) whose press went to the
  // application. See ReportMouse().
  unsigned m_appOwnedButtons{0};
  // Set when the capture was lost while the application owned the left press:
  // the real release that may follow is not a local one.
  bool m_ignoreNextLeftUp{false};
  wxPoint m_lastMouseCell;
  wxPoint m_lastMousePixel;
  unsigned char m_lastMouseModifiers{0};
  // Whether the last right press went to the application. OnContextMenu()
  // uses it, because the menu event arrives after the release.
  bool m_rightPressAppOwned{false};
  int m_scrollOffset{0}; // 0 = at bottom, >0 = scrolled back
  int m_wheelAccum{0};

  wxFont m_defaultFont;
  wxFont m_defaultFontBold;
  wxFont m_defaultFontUnderlined;
  wxFont m_defaultFontBoldUnderlined;
  wxFont m_fallbackFont;
  int m_charW{0};
  int m_charH{0};
  bool m_contextMenuShowing{false};
  wxString m_shell_command;
  std::optional<EnvironmentList> m_environment{std::nullopt};
  std::optional<std::string> m_workingDirectory{std::nullopt};
  wxString m_currentDirectory;
  wxTerminalProgressState m_progressState{wxTerminalProgressState::Hidden};
  int m_progressValue{0};
  wxTerminalProgramStatus m_programStatus;
  bool m_safeDrawing{false};
#if USE_TIMER_REFRESH
  std::atomic_bool m_needsRepaint{true};
  std::unique_ptr<std::thread> m_drawingTimerThread{nullptr};
  std::atomic_bool m_shutdownFlag{false};
#endif
  std::unordered_set<wxChar> m_selectionDelimChars;
  bool m_hasFocusBorder{false};
  Strip m_header;
  Strip m_footer;
  std::vector<wxString> m_pendingCommands;
  bool m_backendReady{false};
  OutputCallback m_outputCallback{nullptr};
  KeyEventFilter m_keyEventFilter{nullptr};
  TextClickedFilter m_textClickedFilter{nullptr};
  std::atomic_bool m_refreshPending{false};
  size_t m_refreshRequested{0};
  size_t m_refreshExecuted{0};
  bool m_showScrollBar{true};

  // --- Incremental rendering state -----------------------------------------
  // Persistent off-screen buffer. Because the buffer is retained between
  // paints, OnPaint only has to redraw the rows that actually changed; the
  // rest of the previous frame is already present in the bitmap.
  wxBitmap m_backingStore;
  // Signature of what was last drawn for each visible screen row. A row is
  // re-rendered only when its prepared cell list differs from the cached one.
  std::vector<std::vector<CellInfo>> m_rowCache;
  // When false the next paint redraws everything (e.g. after theme/font/size
  // changes or the very first paint).
  bool m_renderCacheValid{false};
  // Screen row that held the cursor block on the previous paint, so it can be
  // force-redrawn to erase the old block when the cursor moves.
  int m_lastCursorScreenRow{-1};

  // Force a full redraw on the next paint.
  inline void InvalidateRenderCache() { m_renderCacheValid = false; }

  void OnResizeEndTimer(wxTimerEvent &evt);
  void OnFeedTimer(wxTimerEvent &event);
  // Drains up to kMaxFeedBytesPerTick from m_feedBuffer into the terminal core.
  // Returns true if the buffer still holds data (the timer must re-arm).
  bool ProcessFeedBuffer();
  // Wakes the feed timer if it is idle. Must be called on the GUI thread.
  void WakeFeedTimer();

#if USE_OPENGL
  // --- OpenGL rendering state ----------------------------------------------
  std::unique_ptr<wxGLContext> m_glContext;
  TerminalGLRenderer m_glRenderer;
  bool m_glInitialized{false};
  // Cell metrics last used to rasterize the glyph atlas; when these change the
  // atlas must be flushed so glyphs are re-rasterized at the new size.
  int m_glAtlasCharW{0};
  int m_glAtlasCharH{0};
  wxSize m_glLastClientSize{0, 0};
#endif
  wxTimer m_resizeEndTimer;
  wxTimer m_feedTimer;
  wxTimer
      m_progressTimer; // moves the gauge while the progress is indeterminate
  Gauge *m_progress{nullptr};
  bool m_showProgressBar{false};
  wxColour m_progressColour;   // invalid: the green of the theme
  wxColour m_progressBgColour; // invalid: the background of the theme
  std::mutex m_feedMutex;
  std::string m_feedBuffer;
  std::atomic_bool m_feedShuttingDown{false};
};
