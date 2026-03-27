#pragma once

#include "pty_backend.h"
#include "terminal_core.h"
#include <wx/panel.h>

#include <vector>
#include <wx/timer.h>

#include <memory>

class TerminalView : public wxPanel {
public:
  explicit TerminalView(wxWindow *parent);
  ~TerminalView() override;

  void Feed(const std::string &data);
  bool StartProcess(const std::string &command);
  void SendInput(const std::string &text);
  void SetTerminalSizeFromClient();
  std::string Contents() const;
  void SetTheme(const wxTerminalTheme &theme);
  const wxTerminalTheme &GetTheme() const;

  void ScrollToLastLine();
  std::size_t GetLineCount() const;
  void SetBufferSize(std::size_t maxLines);
  std::size_t GetBufferSize() const;
  void CenterLine(std::size_t line);
  wxString GetLine(std::size_t line) const;
  void SetSelection(std::size_t col, std::size_t row, std::size_t count);
  void ClearSelection();

  // Override to indicate this window can receive keyboard focus
  bool AcceptsFocus() const override { return true; }
  bool AcceptsFocusFromKeyboard() const override { return true; }

  // Override to prevent default navigation behavior for Enter/Tab/Escape
  bool ShouldInheritColours() const override { return true; }
  wxBorder GetDefaultBorder() const override { return wxBORDER_NONE; }

private:
  void OnPaint(wxPaintEvent &evt);
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
    wxRect rect;
    bool active{false};
  };

  struct ApiSelection {
    std::size_t row{0}, col{0}, endCol{0};
    bool active{false};
  };

  terminal::TerminalCore m_core;
  std::unique_ptr<terminal::PtyBackend> m_backend;
  Selection m_selection;
  ApiSelection m_apiSelection;
  bool m_isDragging{false};
  bool m_dirty{true};
  int m_scrollOffset{0}; // 0 = at bottom, >0 = scrolled back
  int m_wheelAccum{0};

  wxFont m_defaultFont;
  wxTimer m_timer;
};
