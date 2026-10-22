#pragma once

#include <wx/event.h>

/// Progress bar state, set by the program with OSC 9;4.
enum class wxTerminalProgressState {
  Hidden = 0,        ///< No progress bar.
  Normal = 1,        ///< Normal progress, value is 0-100.
  Error = 2,         ///< Error, value is 0-100.
  Indeterminate = 3, ///< Busy, no percent known.
  Warning = 4,       ///< Warning or paused, value is 0-100.
};

/// State a program reports with OSC 7501 (Program Status Protocol, rev 0.4).
enum class wxTerminalProgramState {
  Idle,    ///< Waiting for the user, or the user cancelled.
  Working, ///< Busy. GetProgramProgress() may hold a percent.
  Done,    ///< Finished. Stays until the next report.
  Blocked, ///< Waits for the user. See wxTerminalBlockedKind.
  Error,   ///< Failed. Stays until the next report.
  Clear,   ///< The record was removed: nothing to show.
};

/// Why a program is blocked (the "kind" key of OSC 7501).
enum class wxTerminalBlockedKind {
  None,
  Permission,
  Question,
  Auth,
};

/// One OSC 7501 report. A default-constructed value means "no status".
struct wxTerminalProgramStatus {
  wxTerminalProgramState state{wxTerminalProgramState::Clear};
  wxTerminalBlockedKind kind{wxTerminalBlockedKind::None};
  int progress{-1};  ///< 0-100, or -1 when unknown (indeterminate).
  wxString id;       ///< Record path such as "build/test". Empty: root record.
  wxString app;      ///< Name of the program, may be empty.
  wxString title;    ///< Decoded title, may be empty.
  wxString message;  ///< Decoded message, may be empty.
};

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

  void SetProgressState(wxTerminalProgressState state) {
    m_progressState = state;
  }
  wxTerminalProgressState GetProgressState() const { return m_progressState; }

  /// Progress value, 0-100.
  void SetProgressValue(int value) { m_progressValue = value; }
  int GetProgressValue() const { return m_progressValue; }

  /// True if the progress bar should be shown (any state but Hidden).
  bool ShowProgress() const {
    return m_progressState != wxTerminalProgressState::Hidden;
  }
  /// True if the progress bar should be hidden.
  bool HideProgress() const {
    return m_progressState == wxTerminalProgressState::Hidden;
  }

  /// The OSC 7501 report of a wxEVT_TERMINAL_PROGRAM_STATUS event.
  void SetProgramStatus(const wxTerminalProgramStatus &status) {
    m_programStatus = status;
  }
  const wxTerminalProgramStatus &GetProgramStatus() const {
    return m_programStatus;
  }
  wxTerminalProgramState GetProgramState() const {
    return m_programStatus.state;
  }
  wxTerminalBlockedKind GetBlockedKind() const { return m_programStatus.kind; }
  /// 0-100, or -1 when the program did not report a percent.
  int GetProgramProgress() const { return m_programStatus.progress; }
  const wxString &GetProgramId() const { return m_programStatus.id; }
  const wxString &GetProgramApp() const { return m_programStatus.app; }
  const wxString &GetProgramTitle() const { return m_programStatus.title; }
  const wxString &GetProgramMessage() const { return m_programStatus.message; }

private:
  wxTerminalProgressState m_progressState{wxTerminalProgressState::Hidden};
  int m_progressValue{0};
  wxTerminalProgramStatus m_programStatus;
  wxString m_title;
  wxString m_clickedText;
};

/// Terminals's title changed.
wxDECLARE_EVENT(wxEVT_TERMINAL_TITLE_CHANGED, wxTerminalEvent);
/// Current directory changed (OSC 7 or OSC 9;9). GetString() holds the decoded path.
wxDECLARE_EVENT(wxEVT_TERMINAL_CWD_CHANGED, wxTerminalEvent);
/// The program set or cleared a progress bar with OSC 9;4. See
/// GetProgressState() and GetProgressValue() of the event, and
/// wxTerminalViewCtrl::GetProgressState() for the current state.
wxDECLARE_EVENT(wxEVT_TERMINAL_PROGRESS, wxTerminalEvent);
/// The program sent a status report with OSC 7501 (Program Status Protocol, for
/// example Kiro CLI). The event is posted once for each valid report. See
/// GetProgramStatus() of the event; it holds the id, state, kind, progress, app,
/// title and message of the report. wxTerminalViewCtrl::GetProgramStatus()
/// returns the latest report of the root record (no id).
wxDECLARE_EVENT(wxEVT_TERMINAL_PROGRAM_STATUS, wxTerminalEvent);
/// The program copied text with OSC 52, or the text was cleared. GetString()
/// holds the text, or is empty after a clear. The current text is returned by
/// wxTerminalViewCtrl::GetAppSelection().
wxDECLARE_EVENT(wxEVT_TERMINAL_APP_SELECTION_CHANGED, wxTerminalEvent);
/// Terminal terminated.
wxDECLARE_EVENT(wxEVT_TERMINAL_TERMINATED, wxTerminalEvent);
/// User used Ctrl+CLICK on a text in the terminal.
wxDECLARE_EVENT(wxEVT_TERMINAL_TEXT_LINK, wxTerminalEvent);
/// Terminal BELL sound
wxDECLARE_EVENT(wxEVT_TERMINAL_BELL, wxTerminalEvent);
/// The program in the terminal started tracking the mouse (for example tmux or
/// vim with mouse support). Mouse clicks now go to the program. Holding Shift
/// gives the mouse back to the terminal.
///
/// Both events are posted, not sent: a handler can run after the state changed
/// again, so it should not assume that the last event matches the current
/// state. Use wxTerminalViewCtrl::IsMouseTracking() if the current state matters.
wxDECLARE_EVENT(wxEVT_TERMINAL_APP_OWNS_MOUSE, wxTerminalEvent);
/// The program stopped tracking the mouse. The terminal handles it again.
wxDECLARE_EVENT(wxEVT_TERMINAL_APP_RELEASE_MOUSE, wxTerminalEvent);
