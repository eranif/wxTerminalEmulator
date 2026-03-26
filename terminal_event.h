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

private:
  wxString m_title;
};

wxDECLARE_EVENT(wxEVT_TERMINAL_TITLE_CHANGED, wxTerminalEvent);
wxDECLARE_EVENT(wxEVT_TERMINAL_TERMINATED, wxTerminalEvent);
