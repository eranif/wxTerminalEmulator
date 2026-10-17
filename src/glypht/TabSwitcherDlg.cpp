#include "TabSwitcherDlg.h"

#include <wx/sizer.h>

TabSwitcherDlg::TabSwitcherDlg(wxWindow *parent, std::vector<Item> items,
                               bool backwards, DoneCallback onDone,
                               const wxSize &preferredSize)
    : wxDialog(parent, wxID_ANY, _("Switch Tab"), wxDefaultPosition,
               wxDefaultSize, wxCAPTION | wxRESIZE_BORDER | wxSTAY_ON_TOP),
      m_items{std::move(items)}, m_onDone{std::move(onDone)} {
  m_list = new wxDataViewListCtrl(this, wxID_ANY, wxDefaultPosition,
                                  wxDefaultSize, wxDV_SINGLE | wxDV_NO_HEADER);
  m_list->AppendTextColumn(_("Tab"), wxDATAVIEW_CELL_INERT, FromDIP(220));
  m_list->AppendTextColumn(_("Directory"), wxDATAVIEW_CELL_INERT, FromDIP(260));
  for (const auto &item : m_items) {
    wxVector<wxVariant> row;
    row.push_back(wxVariant(item.label));
    row.push_back(wxVariant(item.directory));
    m_list->AppendItem(row);
  }

  auto *sizer = new wxBoxSizer(wxVERTICAL);
  sizer->Add(m_list, 1, wxEXPAND | wxALL, FromDIP(4));
  SetSizer(sizer);
  wxSize size = preferredSize != wxDefaultSize ? preferredSize
                                               : FromDIP(wxSize(520, 260));
  SetClientSize(size);
  CentreOnParent();

  if (!m_items.empty()) {
    const int row = backwards ? static_cast<int>(m_items.size()) - 1
                              : (m_items.size() > 1 ? 1 : 0);
    m_list->SelectRow(row);
    m_list->EnsureVisible(m_list->RowToItem(row));
  }

  Bind(wxEVT_CHAR_HOOK, &TabSwitcherDlg::OnCharHook, this);
  Bind(wxEVT_ACTIVATE, &TabSwitcherDlg::OnActivate, this);
  m_list->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &TabSwitcherDlg::OnItemActivated,
               this);
  wxEvtHandler::AddFilter(this);
}

TabSwitcherDlg::~TabSwitcherDlg() { wxEvtHandler::RemoveFilter(this); }

int TabSwitcherDlg::FilterEvent(wxEvent &event) {
  if (m_done) {
    return Event_Skip;
  }
  const auto type = event.GetEventType();
  auto &key = static_cast<wxKeyEvent &>(event);
  if (type == wxEVT_KEY_UP && key.GetKeyCode() == WXK_RAW_CONTROL) {
    Finish(true);
  } else if (type == wxEVT_CHAR_HOOK && key.GetKeyCode() == WXK_TAB) {
    // On macOS the native list may swallow Ctrl+Tab before our own handler
    // sees it, so take it here, where every key event passes first.
    MoveSelection(key.ShiftDown() ? -1 : 1);
    return Event_Processed;
  }
  return Event_Skip;
}

void TabSwitcherDlg::OnCharHook(wxKeyEvent &event) {
  switch (event.GetKeyCode()) {
  case WXK_TAB:
    MoveSelection(event.ShiftDown() ? -1 : 1);
    break;
  case WXK_RETURN:
  case WXK_NUMPAD_ENTER:
    Finish(true);
    break;
  case WXK_ESCAPE:
    Finish(false);
    break;
  default:
    event.Skip();
    break;
  }
}

void TabSwitcherDlg::OnActivate(wxActivateEvent &event) {
  event.Skip();
  if (event.GetActive()) {
    m_wasActive = true;
  } else if (m_wasActive) {
    Finish(false, true);
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

void TabSwitcherDlg::Finish(bool accept, bool lostFocus) {
  if (m_done) {
    return;
  }
  m_done = true;

  wxWindow *chosen = nullptr;
  const int row = m_list->GetSelectedRow();
  if (accept && row != wxNOT_FOUND) {
    chosen = m_items[static_cast<size_t>(row)].page;
  }

  Hide();
  if (m_onDone) {
    m_onDone(chosen, lostFocus);
  }
  // Destroy later: we may be inside one of our own event handlers.
  CallAfter([this] { Destroy(); });
}
