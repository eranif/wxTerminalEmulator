#pragma once

#include <functional>
#include <vector>
#include <wx/dataview.h>
#include <wx/dialog.h>
#include <wx/event.h>
#include <wx/eventfilter.h>

// A modeless dialog that lists the tabs in most-recently-used order. It is
// shown by Ctrl-Tab (forward) or Shift-Ctrl-Tab (backward). Each Tab press
// moves the selection. Releasing Ctrl, Enter or a double click picks the
// selected tab. Escape or losing the focus cancels.
class TabSwitcherDlg : public wxDialog, public wxEventFilter {
public:
  struct Item {
    wxString label;
    wxString directory;
    wxWindow *page{nullptr};
  };

  // Called once when the dialog is done. `page` is the chosen tab, or nullptr
  // if the user cancelled. `lostFocus` is true if the dialog closed because
  // another window was activated.
  using DoneCallback = std::function<void(wxWindow *page, bool lostFocus)>;

  // `items` are in MRU order: the first item is the current tab. The initial
  // selection is the second item (forward) or the last item (backward).
  // `preferredSize` is used to restore the previously saved dialog size, if any.
  TabSwitcherDlg(wxWindow *parent, std::vector<Item> items, bool backwards,
                 DoneCallback onDone, const wxSize &preferredSize = wxDefaultSize);
  ~TabSwitcherDlg() override;

  // wxEventFilter: sees the Ctrl key release, which the dialog does not get
  // as a normal key event when a child control has the focus.
  int FilterEvent(wxEvent &event) override;

  void FocusList() { m_list->SetFocus(); }
  wxSize GetSavedSize() const { return GetClientSize(); }

private:
  void OnCharHook(wxKeyEvent &event);
  void OnActivate(wxActivateEvent &event);
  void OnItemActivated(wxDataViewEvent &event);
  void MoveSelection(int delta);
  void Finish(bool accept, bool lostFocus = false);

  wxDataViewListCtrl *m_list{nullptr};
  std::vector<Item> m_items;
  DoneCallback m_onDone;
  bool m_wasActive{false};
  bool m_done{false};
};
