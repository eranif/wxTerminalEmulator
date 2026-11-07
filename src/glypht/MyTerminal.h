#pragma once

#include "TabStatusIcon.h"
#include "terminal_event.h"
#include "terminal_view.h"

#include <functional>
#include <memory>
#include <optional>
#include <wx/aui/auibook.h>
#include <wx/string.h>

using EnvironmentList = terminal::PtyBackend::EnvironmentList;

// The tabbed book of the window. The tabs can be moved, split and docked
// inside it, and they are drawn by GlyphTabArt (wxWidgets 3.3 and later).
using TabBook = wxAuiNotebook;

// The terminal of a tab. It handles its own events: the title and the label of
// its tab, and the state of the program (OSC 9;4 and OSC 7501) that it shows as
// an icon on the tab.
class MyTerminal : public wxTerminalViewCtrl {
public:
  // @p parent is the panel of the tab (see TerminalPanel), which is the page of
  // @p notebook.
  MyTerminal(wxWindow *parent, TabBook *notebook, const wxString &shellCommand,
             const std::optional<EnvironmentList> &environment,
             std::optional<wxString> workingDirectory = std::nullopt,
             bool showScrollBar = true);
  ~MyTerminal() override;

  // Called when this terminal got a progress (OSC 9;4) or a program status
  // (OSC 7501) event. The frame uses it to update its status bar.
  void SetProgressCallback(std::function<void(MyTerminal *)> callback);

  void SetTabLabel(const wxString &label);
  void ResetTabLabel();

  // The user-assigned tab label, or empty if the title is driven by the shell.
  const wxString &GetCustomLabel() const { return m_customLabel; }

private:
  void OnTitleChanged(wxTerminalEvent &event);
  void OnProgress(wxTerminalEvent &event);
  void OnProgramStatus(wxTerminalEvent &event);
  // Show the state as an icon on the tab (see TabStatusIcon).
  void SetTabState(wxTerminalProgramState state);
  void NotifyProgress();

  TabBook *m_notebook{nullptr};
  std::unique_ptr<TabStatusIcon> m_statusIcon;
  std::function<void(MyTerminal *)> m_progressCallback;
  wxString m_customLabel;
};
