#pragma once

#include <functional>
#include <vector>
#include <wx/dataview.h>
#include <wx/event.h>
#include <wx/eventfilter.h>
#include <wx/popupwin.h>
#include <wx/timer.h>

// A popup that lists the tabs in most-recently-used order. It is shown by
// Ctrl-Tab (forward) or Shift-Ctrl-Tab (backward). Each Tab press moves the
// selection. Releasing Ctrl, Enter or a double click picks the selected tab.
// Escape or a click outside of the popup cancels.
//
// It is a wxPopupTransientWindow, so it does not take the activation from the
// frame. Create it with `new` and call ShowPopup(); it destroys itself when it
// is done.
class TabSwitcherDlg : public wxPopupTransientWindow, public wxEventFilter {
public:
  struct Item {
    wxString label;
    wxString directory;
    wxWindow *page{nullptr};
  };

  // Called once when the popup is done. `page` is the chosen tab, or nullptr
  // if the user cancelled. `lostFocus` is true if the popup closed because the
  // user clicked outside of it (or another window was activated): the caller
  // should not take the focus back then.
  using DoneCallback = std::function<void(wxWindow *page, bool lostFocus)>;

  // `items` are in MRU order: the first item is the current tab. The initial
  // selection is the second item (forward) or the last item (backward).
  // `preferredSize` is the size of the popup, if given.
  TabSwitcherDlg(wxWindow *parent, std::vector<Item> items, bool backwards,
                 DoneCallback onDone,
                 const wxSize &preferredSize = wxDefaultSize);
  ~TabSwitcherDlg() override;

  // wxEventFilter: sees the keys of the whole application. The popup does not
  // have the keyboard focus, so it gets no key events of its own.
  int FilterEvent(wxEvent &event) override;

  // Centre the popup on its parent and show it.
  void ShowPopup();

  wxSize GetSavedSize() const { return GetClientSize(); }

  void Advance(int delta) { MoveSelection(delta); }

protected:
  // Called after the popup was hidden: by Finish(), or by a click outside.
  void OnDismiss() override;

private:
  void OnItemActivated(wxDataViewEvent &event);
  void OnTimer(wxTimerEvent &event);
  void MoveSelection(int delta);
  void Finish(bool accept, bool lostFocus = false);

  wxDataViewListCtrl *m_list{nullptr};
  std::vector<Item> m_items;
  DoneCallback m_onDone;
  wxTimer m_ctrlKeyTimer;
  bool m_done{false};
};
