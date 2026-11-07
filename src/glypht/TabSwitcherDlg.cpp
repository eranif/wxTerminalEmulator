#include "TabSwitcherDlg.h"

#include "WindowUtils.hpp"

#include <wx/sizer.h>

TabSwitcherDlg::TabSwitcherDlg(wxWindow *parent, std::vector<Item> items,
                               bool backwards, DoneCallback onDone,
                               const wxSize &preferredSize)
    : wxPopupTransientWindow(parent, wxBORDER_DEFAULT),
      m_items{std::move(items)}, m_onDone{std::move(onDone)},
      m_ctrlKeyTimer{this} {
  m_list =
      new wxDataViewListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxDV_SINGLE | wxDV_NO_HEADER | wxDV_ROW_LINES |
                                 wxWANTS_CHARS | wxBORDER_NONE);
  m_list->AppendTextColumn(_("Tab"), wxDATAVIEW_CELL_INERT, FromDIP(220));
  m_list->AppendTextColumn(_("Directory"), wxDATAVIEW_CELL_INERT, FromDIP(260));
  AdjustDataViewAlternateColour(m_list);
  for (const auto &item : m_items) {
    wxVector<wxVariant> row;
    row.push_back(wxVariant(item.label));
    row.push_back(wxVariant(item.directory));
    m_list->AppendItem(row);
  }

  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_list, wxSizerFlags(1).Expand().Border(wxALL, FromDIP(5)));
  SetSizer(sizer);
  SetSize(preferredSize != wxDefaultSize ? preferredSize
                                         : FromDIP(wxSize(520, 260)));

  if (!m_items.empty()) {
    const int row = backwards ? static_cast<int>(m_items.size()) - 1
                              : (m_items.size() > 1 ? 1 : 0);
    m_list->SelectRow(row);
    m_list->EnsureVisible(m_list->RowToItem(row));
  }

  m_list->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &TabSwitcherDlg::OnItemActivated,
               this);
  Bind(wxEVT_TIMER, &TabSwitcherDlg::OnTimer, this);
  wxEvtHandler::AddFilter(this);
}

TabSwitcherDlg::~TabSwitcherDlg() {
  m_ctrlKeyTimer.Stop();
  wxEvtHandler::RemoveFilter(this);
}

void TabSwitcherDlg::ShowPopup() {
  wxWindow *parent = GetParent();
  const wxSize size = GetSize();
  const wxPoint parentPos = parent->ClientToScreen(wxPoint(0, 0));
  const wxSize parentSize = parent->GetClientSize();
  Position(parentPos + wxPoint((parentSize.x - size.x) / 2,
                               (parentSize.y - size.y) / 2),
           wxSize(0, 0));
  Popup(m_list);
  // A shadow and rounded corners (macOS, Windows), after the window exists.
  StylePopupWindow(this, 10.0);

  // The key-up of the Ctrl key may go to the window that has the focus, which
  // is not always seen: poll for it as well.
  m_ctrlKeyTimer.Start(30);
}

int TabSwitcherDlg::FilterEvent(wxEvent &event) {
  if (m_done) {
    return Event_Skip;
  }
  const auto type = event.GetEventType();
  if (type != wxEVT_KEY_UP && type != wxEVT_CHAR_HOOK) {
    return Event_Skip;
  }
  auto &key = static_cast<wxKeyEvent &>(event);
  if (type == wxEVT_KEY_UP) {
    if (key.GetKeyCode() == WXK_RAW_CONTROL) {
      Finish(true);
    }
    return Event_Skip;
  }
  switch (key.GetKeyCode()) {
  case WXK_TAB:
    MoveSelection(key.ShiftDown() ? -1 : 1);
    return Event_Processed;
  case WXK_RETURN:
  case WXK_NUMPAD_ENTER:
    Finish(true);
    return Event_Processed;
  case WXK_ESCAPE:
    Finish(false);
    return Event_Processed;
  default:
    return Event_Skip;
  }
}

void TabSwitcherDlg::OnTimer(wxTimerEvent &) {
  if (!m_done && !wxGetKeyState(WXK_RAW_CONTROL)) {
    Finish(true);
  }
}

void TabSwitcherDlg::OnItemActivated(wxDataViewEvent &) { Finish(true); }

void TabSwitcherDlg::MoveSelection(int delta) {
  const int count = static_cast<int>(m_items.size());
  if (count == 0) {
    return;
  }
  int row = m_list->GetSelectedRow();
  if (row == wxNOT_FOUND) {
    row = 0;
  }
  row = (row + delta + count) % count;
  m_list->SelectRow(row);
  m_list->EnsureVisible(m_list->RowToItem(row));
}

void TabSwitcherDlg::OnDismiss() {
  m_ctrlKeyTimer.Stop();
  // A click outside of the popup: Finish() was not called.
  if (!m_done) {
    m_done = true;
    if (m_onDone) {
      m_onDone(nullptr, true);
    }
  }
  // Destroy later: we may be inside one of our own event handlers.
  CallAfter([this] { Destroy(); });
}

void TabSwitcherDlg::Finish(bool accept, bool lostFocus) {
  if (m_done) {
    return;
  }
  m_done = true;
  m_ctrlKeyTimer.Stop();

  wxWindow *chosen = nullptr;
  const int row = m_list->GetSelectedRow();
  if (accept && row != wxNOT_FOUND) {
    chosen = m_items[static_cast<size_t>(row)].page;
  }

  // Hide the popup first, then switch the tab. OnDismiss() schedules the
  // destruction.
  DismissAndNotify();
  if (m_onDone) {
    m_onDone(chosen, lostFocus);
  }
}
