#pragma once

#include "pty_backend.h"
#include "terminal_core.h"

#include <wx/panel.h>
#include <wx/timer.h>

#include <memory>

class TerminalPanel : public wxPanel {
public:
  explicit TerminalPanel(wxWindow *parent);
  ~TerminalPanel() override;

  void Feed(const std::string &data);
  bool StartProcess(const std::string &command);
  void SendInput(const std::string &text);
  void SetTerminalSizeFromClient();
  std::string Contents() const;

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
  void OnChar(wxKeyEvent &evt);
  void OnTimer(wxTimerEvent &evt);
  void OnMouseClick(wxMouseEvent &evt);
  void OnMouseMove(wxMouseEvent &evt);
  void OnMouseUp(wxMouseEvent &evt);
  void OnRightClick(wxMouseEvent &evt);
  void OnFocus(wxFocusEvent &evt);
  void OnCopy(wxCommandEvent &evt);
  void OnPaste(wxCommandEvent &evt);

  struct Selection {
    int startRow{-1}, startCol{-1};
    int endRow{-1}, endCol{-1};
    bool active{false};
  };

  terminal::TerminalCore m_core;
  wxTimer m_timer;
  std::unique_ptr<terminal::PtyBackend> m_backend;
  bool m_cursorVisible{true};
  Selection m_selection;
  bool m_isDragging{false};
};
