#pragma once

#include "TabAttentionIcon.h"
#include "terminal_event.h"

#include <memory>

/// Shows the state of the program that runs in a page (see
/// wxTerminalProgramState) as an icon on the tab of the page:
///
/// - Working: a spinner (it stays when the page gets the focus, as it only
///   shows a state).
/// - Blocked: a pulse that asks the user to come back (the program waits for
///   them). It is always shown, also on the selected tab, and stays (like the
///   spinner) until the state changes.
/// - Done: a check mark. It is not shown if the user looks at the page (it is
///   selected and the window is active), and it goes away when a window inside
///   the page gets the focus.
/// - Idle, Clear, Error: no icon. A check mark stays until the user comes back
///   to the page.
///
/// The icon that was on the tab before is restored when an icon is removed, and
/// when this object is destroyed. If the page is destroyed first, this object
/// does nothing any more.
class TabStatusIcon {
public:
  /// The time of each frame of the spinner.
  static constexpr int kFrameIntervalMs = 50;

  TabStatusIcon(TabIconBook book, wxWindow *page);
  ~TabStatusIcon();

  TabStatusIcon(const TabStatusIcon &) = delete;
  TabStatusIcon &operator=(const TabStatusIcon &) = delete;

  /// Show the icon of @p state. Only a change of state does something (a
  /// working program reports its progress again and again).
  void SetState(wxTerminalProgramState state);

  /// The last state that was set.
  wxTerminalProgramState GetState() const { return m_state; }

  /// Has the page been destroyed? Then this object does nothing, and can be
  /// deleted.
  bool IsPageGone() const { return m_page == nullptr; }

private:
  void OnPageDestroyed(wxWindowDestroyEvent &event);
  void ShowSpinner();
  void ShowAttention(wxTerminalProgramState state);

  TabIconBook m_book;
  wxWindow *m_page{nullptr}; // nullptr after the page was destroyed
  // The first member is a spinner, the second asks for the user. Their frames
  // are made when they are shown, so they always match the appearance.
  std::unique_ptr<TabAttentionIcon> m_spinner;
  std::unique_ptr<TabAttentionIcon> m_attention;
  wxTerminalProgramState m_attentionState{wxTerminalProgramState::Clear};
  wxTerminalProgramState m_state{wxTerminalProgramState::Clear};
};
