#pragma once

#include "pty_backend.h"
#include "terminal_core.h"

#include <vector>
#include <wx/panel.h>

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
  void OnIdle(wxIdleEvent &evt);
  void OnMouseClick(wxMouseEvent &evt);
  void OnMouseMove(wxMouseEvent &evt);
  void OnMouseUp(wxMouseEvent &evt);
  void OnRightClick(wxMouseEvent &evt);
  void OnMouseWheel(wxMouseEvent &evt);
  void OnFocus(wxFocusEvent &evt);
  void OnCopy(wxCommandEvent &evt);
  void OnPaste(wxCommandEvent &evt);

  struct Selection {
    int startRow{-1}, startCol{-1};
    int endRow{-1}, endCol{-1};
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

  // Command history
  std::vector<std::string> m_commandHistory;
  int m_historyIndex{-1};
  std::string m_currentCommand;
  wxFont m_defaultFont;
};
