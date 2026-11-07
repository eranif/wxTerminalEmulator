#include "MyTerminal.h"

#include <utility>
#include <wx/intl.h>
#include <wx/toplevel.h>

MyTerminal::MyTerminal(wxWindow *parent, TabBook *notebook,
                       const wxString &shellCommand,
                       const std::optional<EnvironmentList> &environment,
                       std::optional<wxString> workingDirectory,
                       bool showScrollBar)
    : wxTerminalViewCtrl(parent, shellCommand, environment, workingDirectory,
                         showScrollBar),
      m_notebook{notebook} {
  // The icon on the tab shows the state of the program. It does nothing once
  // the page (the parent) is destroyed.
  m_statusIcon =
      std::make_unique<TabStatusIcon>(TabIconBook::From(notebook), parent);
  Bind(wxEVT_TERMINAL_TITLE_CHANGED, &MyTerminal::OnTitleChanged, this);
  Bind(wxEVT_TERMINAL_PROGRAM_STATUS, &MyTerminal::OnProgramStatus, this);
  Bind(wxEVT_TERMINAL_PROGRESS, &MyTerminal::OnProgress, this);
}

MyTerminal::~MyTerminal() = default;

void MyTerminal::SetProgressCallback(
    std::function<void(MyTerminal *)> callback) {
  m_progressCallback = std::move(callback);
}

// OSC 9;4: the progress bar of the program. Claude Code stops reporting with
// OSC 7501 once a session moves to the background
// (anthropics/claude-code#106772) but still sends OSC 9;4. A bar that is
// removed while the program was busy means it is done.
void MyTerminal::OnProgress(wxTerminalEvent &event) {
  event.Skip();
  const auto previous = m_statusIcon->GetState();
  switch (event.GetProgressState()) {
  case wxTerminalProgressState::Hidden:
    if (previous == wxTerminalProgramState::Working ||
        previous == wxTerminalProgramState::Blocked) {
      SetTabState(wxTerminalProgramState::Done);
    }
    break;
  case wxTerminalProgressState::Normal:
  case wxTerminalProgressState::Indeterminate:
  case wxTerminalProgressState::Warning:
    SetTabState(wxTerminalProgramState::Working);
    break;
  case wxTerminalProgressState::Error:
    break;
  }
  NotifyProgress();
}

// OSC 7501: show the state of the program as an icon on the tab.
void MyTerminal::OnProgramStatus(wxTerminalEvent &event) {
  event.Skip();
  SetTabState(event.GetProgramState());
  NotifyProgress();
}

void MyTerminal::SetTabState(wxTerminalProgramState state) {
  const bool changed = m_statusIcon->GetState() != state;
  m_statusIcon->SetState(state);

  // A program that waits for the user, or that is done, while the window is in
  // the background: bounce the icon in the dock (macOS) or flash the button in
  // the task bar (Windows). Only a change of state asks (a program reports its
  // status again and again), and the system stops it when the user comes back.
  if (changed && (state == wxTerminalProgramState::Blocked ||
                  state == wxTerminalProgramState::Done)) {
    auto *top = wxDynamicCast(wxGetTopLevelParent(this), wxTopLevelWindow);
    if (top && !top->IsActive()) {
      top->RequestUserAttention(wxUSER_ATTENTION_INFO);
    }
  }
}

void MyTerminal::NotifyProgress() {
  if (m_progressCallback) {
    m_progressCallback(this);
  }
}

void MyTerminal::OnTitleChanged(wxTerminalEvent &event) {
  if (!m_customLabel.empty()) {
    return;
  }

  wxString title = event.GetTitle();
  title.Trim().Trim(false);
  if (title.empty()) {
    title = _("Terminal");
  }
  int sel = m_notebook->FindPage(GetParent());
  if (sel != wxNOT_FOUND) {
    m_notebook->SetPageText(sel, title);
  }
}

void MyTerminal::SetTabLabel(const wxString &label) {
  wxString l = label;
  l.Trim().Trim(false);
  if (l.empty()) {
    return;
  }
  m_customLabel = l;
  int sel = m_notebook->FindPage(GetParent());
  if (sel != wxNOT_FOUND) {
    m_notebook->SetPageText(sel, m_customLabel);
  }
}

void MyTerminal::ResetTabLabel() {
  if (m_customLabel.empty()) {
    return;
  }
  m_customLabel.clear();
  int sel = m_notebook->FindPage(GetParent());
  if (sel != wxNOT_FOUND) {
    m_notebook->SetPageText(sel, _("Terminal"));
  }
}
