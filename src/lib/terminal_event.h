#pragma once

#include <wx/event.h>

class wxTerminalEvent : public wxCommandEvent {
public:
  wxTerminalEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}

  wxTerminalEvent(const wxTerminalEvent &other) = default;

  wxEvent *Clone() const override { return new wxTerminalEvent(*this); }

  void SetTitle(const wxString &title) { m_title = title; }
  const wxString &GetTitle() const { return m_title; }

  void SetClickedText(const wxString &clickedText) {
    m_clickedText = clickedText;
  }
  const wxString &GetClickedText() const { return m_clickedText; }

private:
  wxString m_title;
  wxString m_clickedText;
};

/// Terminals's title changed.
wxDECLARE_EVENT(wxEVT_TERMINAL_TITLE_CHANGED, wxTerminalEvent);
/// Terminal terminated.
wxDECLARE_EVENT(wxEVT_TERMINAL_TERMINATED, wxTerminalEvent);
/// User used Ctrl+CLICK on a text in the terminal.
wxDECLARE_EVENT(wxEVT_TERMINAL_TEXT_LINK, wxTerminalEvent);
/// Terminal BELL sound
wxDECLARE_EVENT(wxEVT_TERMINAL_BELL, wxTerminalEvent);
