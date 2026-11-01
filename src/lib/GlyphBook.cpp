#include "GlyphBook.h"

#include <algorithm>
#include <memory>
#include <vector>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/wupdlock.h>

wxDEFINE_EVENT(wxEVT_GLYPHBOOK_PAGE_CLOSE, wxBookCtrlEvent);
wxDEFINE_EVENT(wxEVT_GLYPHBOOK_PAGE_CLOSED, wxBookCtrlEvent);
wxDEFINE_EVENT(wxEVT_GLYPHBOOK_TAB_RIGHT_UP, wxBookCtrlEvent);
wxDEFINE_EVENT(wxEVT_GLYPHBOOK_TAB_MIDDLE_UP, wxBookCtrlEvent);

namespace {

constexpr long kSideFlags =
    wxAUI_NB_TOP | wxAUI_NB_BOTTOM | wxAUI_NB_LEFT | wxAUI_NB_RIGHT;

} // namespace

GlyphBook::GlyphBook() { Init(); }

GlyphBook::GlyphBook(wxWindow *parent, wxWindowID id, const wxPoint &pos,
                     const wxSize &size, long style, const wxString &name) {
  Init();
  Create(parent, id, pos, size, style, name);
}

void GlyphBook::Init() {
  m_pages = nullptr;
  m_tabBar = nullptr;
}

bool GlyphBook::Create(wxWindow *parent, wxWindowID id, const wxPoint &pos,
                       const wxSize &size, long style, const wxString &name) {
  m_style = style;
  if (!wxBookCtrlBase::Create(parent, id, pos, size, style, name)) {
    return false;
  }
  m_style = style;

  m_tabBar = new GlyphTabBar(this, m_tabBarWidth);
  m_pages = new wxSimplebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                             wxBORDER_NONE);

  GlyphTabBar::Callbacks callbacks;
  callbacks.select = [this](size_t index) {
    if (static_cast<int>(index) == GetSelection()) {
      // A click on the current tab gives the focus to its page.
      GetPage(index)->SetFocus();
    } else {
      SetSelection(index);
    }
  };
  callbacks.close = [this](size_t index) { ClosePage(index); };
  callbacks.middleUp = [this](size_t index) {
    // Like wxAuiNotebook: the application can handle the click itself.
    bool handled = false;
    SendEvent(wxEVT_GLYPHBOOK_TAB_MIDDLE_UP, static_cast<int>(index),
              wxNOT_FOUND, &handled);
    if (!handled && (m_style & wxAUI_NB_MIDDLE_CLICK_CLOSE)) {
      ClosePage(index);
    }
  };
  callbacks.rightUp = [this](size_t index) {
    SendEvent(wxEVT_GLYPHBOOK_TAB_RIGHT_UP, static_cast<int>(index));
  };
  m_tabBar->SetCallbacks(std::move(callbacks));

  Bind(wxEVT_SIZE, &GlyphBook::OnSize, this);
  Bind(wxEVT_SYS_COLOUR_CHANGED, &GlyphBook::OnSysColourChanged, this);

  SetTheme(wxTerminalTheme::MakeDarkTheme());
  UpdateLayout();
  return true;
}

// Appearance
// ----------

void GlyphBook::SetTheme(const wxTerminalTheme &theme, const wxColour &outline,
                         GlyphTabPainter::Style style,
                         const wxColour &barColour) {
  m_theme = theme;
  m_outline = outline;
  m_tabStyle = style;
  m_barColour = barColour;
  m_themed = true;
  RebuildPainter();
}

void GlyphBook::RebuildPainter() {
  auto painter = std::make_unique<GlyphTabPainter>(m_theme, m_tabStyle);
  if (m_outline.IsOk()) {
    painter->SetOutlineColour(m_outline);
  }
  // Both styles: the bar is the button face, unless a colour is given.
  painter->SetBarColour(
      m_barColour.IsOk() ? m_barColour
                         : wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
  ApplyPainter(std::move(painter));
}

void GlyphBook::OnSysColourChanged(wxSysColourChangedEvent &event) {
  event.Skip();
  // The button face changes with the appearance of the app, so the bar of the
  // tabs must be made again.
  if (m_themed && !m_barColour.IsOk()) {
    RebuildPainter();
  }
}

void GlyphBook::SetPainter(GlyphTabPainter *painter) {
  wxCHECK_RET(painter, wxS("the tab painter must not be null"));
  // The painter is the one of the application now: it is not made again.
  m_themed = false;
  ApplyPainter(std::unique_ptr<GlyphTabPainter>(painter));
}

void GlyphBook::ApplyPainter(std::unique_ptr<GlyphTabPainter> painter) {
  m_tabBar->SetPainter(std::move(painter));
  DoInvalidateBestSize();
  Layout();
}

void GlyphBook::SetTabPosition(wxDirection position) {
  long style = m_style & ~kSideFlags;
  switch (position) {
  case wxLEFT:
    style |= wxAUI_NB_LEFT;
    break;
  case wxRIGHT:
    style |= wxAUI_NB_RIGHT;
    break;
  case wxBOTTOM:
    style |= wxAUI_NB_BOTTOM;
    break;
  default:
    style |= wxAUI_NB_TOP;
    break;
  }
  SetWindowStyleFlag(style);
}

wxDirection GlyphBook::GetTabPosition() const {
  if (m_style & wxAUI_NB_LEFT) {
    return wxLEFT;
  }
  if (m_style & wxAUI_NB_RIGHT) {
    return wxRIGHT;
  }
  return (m_style & wxAUI_NB_BOTTOM) ? wxBOTTOM : wxTOP;
}

bool GlyphBook::HasVerticalTabs() const {
  const wxDirection position = GetTabPosition();
  return position == wxLEFT || position == wxRIGHT;
}

void GlyphBook::SetWindowStyleFlag(long style) {
  // Called by Create() before the bar exists.
  m_style = style;
  wxBookCtrlBase::SetWindowStyleFlag(style);
  if (m_tabBar) {
    UpdateLayout();
  }
}

void GlyphBook::SetTabBarWidth(int width) {
  m_tabBarWidth = width;
  if (m_tabBar) {
    m_tabBar->SetBarWidth(width);
    m_tabBarWidth = m_tabBar->GetBarWidth();
  }
}

int GlyphBook::GetTabBarWidth() const {
  return m_tabBar ? m_tabBar->GetBarWidth() : m_tabBarWidth;
}

void GlyphBook::SetTabBarWidthChangedCallback(
    std::function<void(int)> callback) {
  m_tabBar->SetWidthChangedCallback(std::move(callback));
}

void GlyphBook::UpdateLayout() {
  const wxDirection position = GetTabPosition();
  m_tabBar->SetCloseButtons((m_style & wxAUI_NB_CLOSE_ON_ALL_TABS) != 0,
                            (m_style & wxAUI_NB_CLOSE_ON_ACTIVE_TAB) != 0);
  m_tabBar->SetSide(position);

  // The bar is before the pages for the left and top sides, after them for the
  // others.
  auto *sizer = new wxBoxSizer(HasVerticalTabs() ? wxHORIZONTAL : wxVERTICAL);
  if (position == wxLEFT || position == wxTOP) {
    sizer->Add(m_tabBar, wxSizerFlags().Expand());
    sizer->Add(m_pages, wxSizerFlags(1).Expand());
  } else {
    sizer->Add(m_pages, wxSizerFlags(1).Expand());
    sizer->Add(m_tabBar, wxSizerFlags().Expand());
  }
  SetSizer(sizer);
  DoInvalidateBestSize();
  Layout();
  Refresh();
}

void GlyphBook::OnSize(wxSizeEvent &event) {
  event.Skip();
  if (GetSizer()) {
    Layout();
  }
}

wxSize GlyphBook::DoGetBestSize() const {
  wxSize size = m_pages ? m_pages->GetBestSize() : wxSize();
  if (m_tabBar) {
    const wxSize bar = m_tabBar->GetEffectiveMinSize();
    if (HasVerticalTabs()) {
      size.x += bar.x;
    } else {
      size.y += bar.y;
    }
  }
  return size;
}

// Events
// ------

bool GlyphBook::SendEvent(wxEventType type, int selection, int oldSelection,
                          bool *handled) {
  wxBookCtrlEvent event(type, GetId(), selection, oldSelection);
  event.SetEventObject(this);
  const bool processed = ProcessWindowEvent(event);
  if (handled) {
    *handled = processed;
  }
  return event.IsAllowed();
}

// Pages
// -----

bool GlyphBook::AddPage(wxWindow *page, const wxString &caption, bool select,
                        const wxBitmapBundle &bitmap) {
  return InsertPage(GetPageCount(), page, caption, select, bitmap);
}

bool GlyphBook::InsertPage(size_t pageIdx, wxWindow *page,
                           const wxString &caption, bool select,
                           const wxBitmapBundle &bitmap) {
  wxCHECK_MSG(page, false, wxS("the page must not be null"));
  wxCHECK_MSG(FindPage(page) == wxNOT_FOUND, false,
              wxS("the page is already in the book"));
  pageIdx = std::min(pageIdx, GetPageCount());
  if (page->GetParent() != m_pages) {
    page->Reparent(m_pages);
  }
  // The pages of the simplebook are in the order they were added; the tabs
  // are in the order of the user.
  if (!m_pages->AddPage(page, caption, false)) {
    return false;
  }
  const bool first = GetPageCount() == 0;
  m_tabBar->Insert(pageIdx, page, caption, bitmap);
  DoInvalidateBestSize();

  if (first) {
    // The first page is shown, without events.
    DoSetSelection(pageIdx, false);
  } else if (select) {
    SetSelection(pageIdx);
  } else {
    page->Hide();
    ShowPage(GetSelection());
  }
  return true;
}

bool GlyphBook::AddPage(wxWindow *page, const wxString &text, bool select,
                        int imageId) {
  return AddPage(page, text, select, GetBitmapBundle(imageId));
}

bool GlyphBook::InsertPage(size_t index, wxWindow *page, const wxString &text,
                           bool select, int imageId) {
  return InsertPage(index, page, text, select, GetBitmapBundle(imageId));
}

wxWindow *GlyphBook::RemoveTab(size_t index, bool destroy) {
  if (index >= GetPageCount()) {
    return nullptr;
  }
  // One update for the whole change: the tab bar, the page that goes away and
  // the page that takes its place.
  wxWindowUpdateLocker locker(this);
  wxWindow *const window = m_tabBar->GetWindow(index);
  const bool wasSelected = static_cast<int>(index) == GetSelection();
  m_tabBar->Remove(index);

  // The tab that took its place, or the last one. It is selected before the
  // simplebook loses the page: otherwise the simplebook would choose a page by
  // its own order (the order the pages were added, not the order of the tabs)
  // and give it the focus. This change cannot be vetoed, because the old tab
  // is gone; only the page changed event is sent, after the removal.
  int newSelection = wxNOT_FOUND;
  if (wasSelected && GetPageCount() > 0) {
    newSelection = static_cast<int>(std::min(index, GetPageCount() - 1));
    DoSetSelection(static_cast<size_t>(newSelection), false);
  }

  const int pageIndex = m_pages->FindPage(window);
  if (destroy) {
    m_pages->DeletePage(pageIndex);
  } else {
    m_pages->RemovePage(pageIndex);
    // The caller owns the page now, so it is not a child of the pages.
    window->Reparent(this);
  }
  DoInvalidateBestSize();

  if (newSelection != wxNOT_FOUND) {
    // The application is told, like with wxAuiNotebook.
    SendEvent(wxEVT_BOOKCTRL_PAGE_CHANGED, newSelection);
  }
  return window;
}

bool GlyphBook::DeletePage(size_t page) {
  return RemoveTab(page, true) != nullptr;
}

wxWindow *GlyphBook::DoRemovePage(size_t page) {
  return RemoveTab(page, false);
}

bool GlyphBook::DeleteAllPages() {
  m_tabBar->Clear();
  const bool ok = m_pages->DeleteAllPages();
  DoInvalidateBestSize();
  return ok;
}

void GlyphBook::ClosePage(size_t index) {
  if (index >= GetPageCount()) {
    return;
  }
  // The same events as the close button of wxAuiNotebook.
  if (!SendEvent(wxEVT_GLYPHBOOK_PAGE_CLOSE, static_cast<int>(index))) {
    return;
  }
  // A handler can close pages itself.
  if (index >= GetPageCount()) {
    return;
  }
  DeletePage(index);
  SendEvent(wxEVT_GLYPHBOOK_PAGE_CLOSED, static_cast<int>(index));
}

size_t GlyphBook::GetPageCount() const {
  return m_tabBar ? m_tabBar->GetCount() : 0;
}

wxWindow *GlyphBook::GetPage(size_t pageIdx) const {
  return pageIdx < GetPageCount() ? m_tabBar->GetWindow(pageIdx) : nullptr;
}

int GlyphBook::FindPage(const wxWindow *page) const {
  return m_tabBar ? m_tabBar->Find(page) : wxNOT_FOUND;
}

bool GlyphBook::SetPageText(size_t page, const wxString &text) {
  if (page >= GetPageCount()) {
    return false;
  }
  m_tabBar->SetCaption(page, text);
  return true;
}

wxString GlyphBook::GetPageText(size_t pageIdx) const {
  return pageIdx < GetPageCount() ? m_tabBar->GetCaption(pageIdx) : wxString();
}

bool GlyphBook::SetPageToolTip(size_t page, const wxString &text) {
  if (page >= GetPageCount()) {
    return false;
  }
  m_tabBar->SetTooltip(page, text);
  return true;
}

wxString GlyphBook::GetPageToolTip(size_t pageIdx) const {
  return pageIdx < GetPageCount() ? m_tabBar->GetTooltip(pageIdx) : wxString();
}

bool GlyphBook::SetPageBitmap(size_t page, const wxBitmapBundle &bitmap) {
  if (page >= GetPageCount()) {
    return false;
  }
  m_tabBar->SetBitmap(page, bitmap);
  return true;
}

wxBitmap GlyphBook::GetPageBitmap(size_t pageIdx) const {
  if (pageIdx >= GetPageCount()) {
    return wxBitmap();
  }
  const wxBitmapBundle &bundle = m_tabBar->GetBitmap(pageIdx);
  return bundle.IsOk() ? bundle.GetBitmapFor(this) : wxBitmap();
}

bool GlyphBook::SetPageImage(size_t page, int imageId) {
  return SetPageBitmap(page, GetBitmapBundle(imageId));
}

int GlyphBook::GetPageImage(size_t WXUNUSED(page)) const { return wxNOT_FOUND; }

void GlyphBook::SetPageSize(const wxSize &WXUNUSED(size)) {}

// Selection
// ---------

int GlyphBook::GetSelection() const {
  return m_tabBar ? m_tabBar->GetSelection() : wxNOT_FOUND;
}

void GlyphBook::ShowPage(int index) {
  if (index < 0 || index >= static_cast<int>(GetPageCount())) {
    return;
  }
  wxWindow *const window = m_tabBar->GetWindow(static_cast<size_t>(index));
  const int pageIndex = m_pages->FindPage(window);
  if (pageIndex != wxNOT_FOUND) {
    m_pages->ChangeSelection(static_cast<size_t>(pageIndex));
  }
  // A page that was added while the book was hidden may have an old size.
  window->SetSize(m_pages->GetClientRect());
}

int GlyphBook::DoSetSelection(size_t page, bool sendEvents) {
  const int old = GetSelection();
  if (page >= GetPageCount() || static_cast<int>(page) == old) {
    return old;
  }
  const int selection = static_cast<int>(page);
  if (sendEvents && !SendEvent(wxEVT_BOOKCTRL_PAGE_CHANGING, selection, old)) {
    return old;
  }
  // The tab bar and the pages change together, and are painted once, when the
  // handler of the event is done.
  wxWindowUpdateLocker locker(this);
  m_tabBar->SetSelection(selection);
  ShowPage(selection);
  if (sendEvents) {
    SendEvent(wxEVT_BOOKCTRL_PAGE_CHANGED, selection, old);
  }
  return old;
}

int GlyphBook::SetSelection(size_t page) { return DoSetSelection(page, true); }

int GlyphBook::ChangeSelection(size_t page) {
  return DoSetSelection(page, false);
}

int GlyphBook::HitTest(const wxPoint &pt, long *flags) const {
  if (flags) {
    *flags = wxBK_HITTEST_NOWHERE;
  }
  if (m_tabBar->GetRect().Contains(pt)) {
    const int index = m_tabBar->HitTest(pt - m_tabBar->GetPosition());
    if (flags && index != wxNOT_FOUND) {
      *flags = wxBK_HITTEST_ONITEM;
    }
    return index;
  }
  if (flags && m_pages->GetRect().Contains(pt)) {
    *flags = wxBK_HITTEST_ONPAGE;
  }
  return wxNOT_FOUND;
}

// Layout
// ------

BookLayout GlyphBook::SaveLayout() const {
  BookLayout layout;
  const size_t count = GetPageCount();
  layout.tabs.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    BookLayout::Tab tab;
    tab.label = GetPageText(i);
    tab.selected = static_cast<int>(i) == GetSelection();
    layout.tabs.push_back(std::move(tab));
  }
  return layout;
}

void GlyphBook::RestoreLayout(const BookLayout &layout) {
  const size_t count = std::min(layout.tabs.size(), GetPageCount());
  int selected = wxNOT_FOUND;
  for (size_t i = 0; i < count; ++i) {
    const BookLayout::Tab &tab = layout.tabs[i];
    if (!tab.label.empty()) {
      SetPageText(i, tab.label);
    }
    if (tab.selected && selected == wxNOT_FOUND) {
      selected = static_cast<int>(i);
    }
  }
  if (selected != wxNOT_FOUND) {
    SetSelection(static_cast<size_t>(selected));
  }
}
