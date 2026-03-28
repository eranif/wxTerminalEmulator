#pragma once

#include "pty_backend.h"
#include "terminal_core.h"
#include <wx/dcbuffer.h>
#include <wx/dcgraph.h>
#include <wx/panel.h>

#include <wx/dc.h>
#include <wx/timer.h>

#include <memory>

class TerminalView : public wxPanel {
public:
  explicit TerminalView(wxWindow *parent);
  ~TerminalView() override;

  void Feed(const std::string &data);
  bool StartProcess(const std::string &command);
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
  std::string Contents() const;

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
  void SendCtrlC();

  void SetTheme(const wxTerminalTheme &theme);
  const wxTerminalTheme &GetTheme() const;

  void ScrollToLastLine();
  std::size_t GetLineCount() const;
  void SetBufferSize(std::size_t maxLines);
  std::size_t GetBufferSize() const;
  void CenterLine(std::size_t line);
  wxString GetLine(std::size_t line) const;
  void SetUserSelection(std::size_t col, std::size_t row, std::size_t count);
  void ClearUserSelection();
  void ClearMouseSelection();
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
  wxColour GetColourFromTheme(std::optional<terminal::ColourSpec> spec,
                              bool foreground) const;
  void OnPaint(wxPaintEvent &evt);
  void RenderRaw(wxDC &dc, int y, int rowIdx,
                 const std::vector<terminal::Cell> &row,
                 size_t &draw_text_calls);
  void OnSize(wxSizeEvent &evt);
  void OnCharHook(wxKeyEvent &evt);
  void OnKeyDown(wxKeyEvent &evt);
  void OnTimer(wxTimerEvent &evt);
  void OnMouseLeftDown(wxMouseEvent &evt);
  void OnMouseMove(wxMouseEvent &evt);
  void OnMouseUp(wxMouseEvent &evt);
  void OnRightClick(wxMouseEvent &evt);
  void OnMouseWheel(wxMouseEvent &evt);
  void OnFocus(wxFocusEvent &evt);
  void OnCopy(wxCommandEvent &evt);
  void OnPaste(wxCommandEvent &evt);
  void DebugDumpViewArea();
  void UpdateFontCache();
  const wxFont &GetCachedFont(bool bold, bool underlined) const;

  wxRect ViewCellToPixelsRect(const wxRect &viewrect) const;
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
  struct Selection {
    wxRect rect; // Logical selection (rows / cols)
    bool active{false};
    inline bool empty() const { return rect.width == 0 && rect.height == 0; }
    inline void clear() {
      rect = {};
      active = false;
    }
  };

  struct ApiSelection {
    std::size_t row{0}, col{0}, endCol{0};
    bool active{false};
  };

  terminal::TerminalCore m_core;
  std::unique_ptr<terminal::PtyBackend> m_backend;
  Selection m_selection;
  ApiSelection m_userSelection;
  bool m_isDragging{false};
  bool m_dirty{true};
  int m_scrollOffset{0}; // 0 = at bottom, >0 = scrolled back
  int m_wheelAccum{0};

  wxFont m_defaultFont;
  wxFont m_defaultFontBold;
  wxFont m_defaultFontUnderlined;
  wxFont m_defaultFontBoldUnderlined;
  wxTimer m_timer;
  int m_charW{0};
  int m_charH{0};
};
