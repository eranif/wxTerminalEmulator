#include "GlyphBook.h"

#if wxCHECK_VERSION(3, 3, 0)

#include <memory>
#include <vector>
#include <wx/settings.h>
#include <wx/sizer.h>

namespace {

// The events of the notebook that the book sends again.
const std::vector<wxEventTypeTag<wxAuiNotebookEvent>> &ForwardedEvents() {
  static const std::vector<wxEventTypeTag<wxAuiNotebookEvent>> events = {
      wxEVT_AUINOTEBOOK_PAGE_CLOSE,    wxEVT_AUINOTEBOOK_PAGE_CHANGED,
      wxEVT_AUINOTEBOOK_PAGE_CHANGING, wxEVT_AUINOTEBOOK_PAGE_CLOSED,
      wxEVT_AUINOTEBOOK_BUTTON,        wxEVT_AUINOTEBOOK_BEGIN_DRAG,
      wxEVT_AUINOTEBOOK_END_DRAG,      wxEVT_AUINOTEBOOK_DRAG_MOTION,
      wxEVT_AUINOTEBOOK_ALLOW_DND,     wxEVT_AUINOTEBOOK_TAB_MIDDLE_DOWN,
      wxEVT_AUINOTEBOOK_TAB_MIDDLE_UP, wxEVT_AUINOTEBOOK_TAB_RIGHT_DOWN,
      wxEVT_AUINOTEBOOK_TAB_RIGHT_UP,  wxEVT_AUINOTEBOOK_DRAG_DONE,
      wxEVT_AUINOTEBOOK_BG_DCLICK};
  return events;
}

constexpr long kSideFlags =
    wxAUI_NB_TOP | wxAUI_NB_BOTTOM | wxAUI_NB_LEFT | wxAUI_NB_RIGHT;
// The flags that need tabs that can be split or moved.
constexpr long kSplitFlags =
    wxAUI_NB_TAB_SPLIT | wxAUI_NB_TAB_MOVE | wxAUI_NB_TAB_EXTERNAL_MOVE;

} // namespace

GlyphBook::GlyphBook() { Init(); }

GlyphBook::GlyphBook(wxWindow *parent, wxWindowID id, const wxPoint &pos,
                     const wxSize &size, long style, const wxString &name) {
  Init();
  Create(parent, id, pos, size, style, name);
  // Disable AUI's behavior: when a non active child accepts the focus, AUI
  // changes the selection.
  m_notebook->Bind(wxEVT_CHILD_FOCUS, &GlyphBook::OnChildFocus, this);
}

GlyphBook::~GlyphBook() {
  // The notebook is destroyed after this destructor: it must not call us.
  m_notebook->Unbind(wxEVT_CHILD_FOCUS, &GlyphBook::OnChildFocus, this);
  if (m_notebook) {
    for (const auto &type : ForwardedEvents()) {
      m_notebook->Unbind(type, &GlyphBook::ForwardEvent, this);
    }
  }
}

void GlyphBook::Init() {
  m_notebook = nullptr;
  m_tabBar = nullptr;
}

bool GlyphBook::Create(wxWindow *parent, wxWindowID id, const wxPoint &pos,
                       const wxSize &size, long style, const wxString &name) {
  m_style = style;
  if (!wxBookCtrlBase::Create(parent, id, pos, size, style, name)) {
    return false;
  }
  m_style = style;

  m_notebook = new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition,
                                 wxDefaultSize, GetNotebookStyle());
  // The bar binds the events of the notebook before the book does, so it is
  // up to date when a handler of the application runs.
  m_tabBar = new GlyphTabBar(this, m_notebook, m_tabBarWidth);
  m_tabBar->Hide();

  for (const auto &type : ForwardedEvents()) {
    m_notebook->Bind(type, &GlyphBook::ForwardEvent, this);
  }
  Bind(wxEVT_SIZE, &GlyphBook::OnSize, this);
  Bind(wxEVT_SYS_COLOUR_CHANGED, &GlyphBook::OnSysColourChanged, this);

  auto *sizer = new wxBoxSizer(wxHORIZONTAL);
  sizer->Add(m_tabBar, wxSizerFlags().Expand());
  sizer->Add(m_notebook, wxSizerFlags(1).Expand());
  SetSizer(sizer);

  SetTheme(wxTerminalTheme::MakeDarkTheme());
  UpdateLayout();
  return true;
}

void GlyphBook::OnChildFocus(wxChildFocusEvent &event) { wxUnusedVar(event); }

// Appearance
// ----------

void GlyphBook::SetTheme(const wxTerminalTheme &theme, const wxColour &outline,
                         GlyphTabArt::Style style, const wxColour &barColour) {
  m_theme = theme;
  m_outline = outline;
  m_artStyle = style;
  m_barColour = barColour;
  m_themed = true;
  RebuildArt();
}

void GlyphBook::RebuildArt() {
  auto art = std::make_unique<GlyphTabArt>(m_theme, m_artStyle);
  if (m_outline.IsOk()) {
    art->SetOutlineColour(m_outline);
  }
  // Both styles: the bar is the button face, unless a colour is given.
  art->SetBarColour(m_barColour.IsOk()
                        ? m_barColour
                        : wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
  ApplyArt(art.release());
}

void GlyphBook::OnSysColourChanged(wxSysColourChangedEvent &event) {
  event.Skip();
  // The button face changes with the appearance of the app, so the bar of the
  // tabs must be made again.
  if (m_themed && !m_barColour.IsOk()) {
    RebuildArt();
  }
}

void GlyphBook::SetArtProvider(GlyphTabArt *art) {
  // The art is the one of the application now: it is not made again.
  m_themed = false;
  ApplyArt(art);
}

void GlyphBook::ApplyArt(GlyphTabArt *art) {
  wxCHECK_RET(art, wxS("the tab art must not be null"));
  m_tabBar->SetArt(
      std::unique_ptr<GlyphTabArt>(static_cast<GlyphTabArt *>(art->Clone())));
  m_notebook->SetArtProvider(art);
  m_notebook->Refresh();
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
  // Called by Create() before the notebook exists.
  m_style = style;
  wxBookCtrlBase::SetWindowStyleFlag(style);
  UpdateLayout();
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

long GlyphBook::GetNotebookStyle() const {
  long style = m_style & ~kSideFlags;
  style |= GetTabPosition() == wxBOTTOM ? wxAUI_NB_BOTTOM : wxAUI_NB_TOP;
  if (HasVerticalTabs()) {
    style &= ~kSplitFlags;
  }
  return style;
}

void GlyphBook::UpdateLayout() {
  if (!m_notebook) {
    return;
  }
  const bool vertical = HasVerticalTabs();
  if (vertical && m_notebook->GetPageCount() > 0) {
    // The bar shows the pages of one tab control.
    m_notebook->UnsplitAll();
  }
  m_notebook->SetWindowStyleFlag(GetNotebookStyle());
  // With the bar, the notebook hides its own tabs.
  m_notebook->SetTabCtrlHeight(vertical ? 0 : -1);

  if (wxSizer *sizer = GetSizer()) {
    // The bar is first for the left side and last for the right side.
    sizer->Detach(m_tabBar);
    sizer->Insert(GetTabPosition() == wxRIGHT ? 1 : 0, m_tabBar,
                  wxSizerFlags().Expand());
    m_tabBar->SetSide(GetTabPosition() == wxRIGHT ? wxRIGHT : wxLEFT);
    m_tabBar->Show(vertical);
    Layout();
  }
  if (vertical) {
    m_tabBar->SyncWithNotebook();
  }
  m_notebook->Refresh();
}

void GlyphBook::SyncTabBar() {
  if (m_tabBar && m_tabBar->IsShown()) {
    m_tabBar->SyncWithNotebook();
  }
}

void GlyphBook::OnSize(wxSizeEvent &event) {
  event.Skip();
  if (GetSizer()) {
    Layout();
  }
}

wxSize GlyphBook::DoGetBestSize() const {
  wxSize size = m_notebook ? m_notebook->GetBestSize() : wxSize();
  if (m_tabBar && m_tabBar->IsShown()) {
    size.x += m_tabBar->GetEffectiveMinSize().x;
  }
  return size;
}

// Events
// ------

void GlyphBook::ForwardEvent(wxAuiNotebookEvent &event) {
  // The event goes to the parents of the book from here, with the book as the
  // source. Without this, the original would go to them too, from the
  // notebook.
  event.StopPropagation();

  std::unique_ptr<wxAuiNotebookEvent> copy{
      static_cast<wxAuiNotebookEvent *>(event.Clone())};
  copy->SetEventObject(this);
  copy->SetId(GetId());
  const bool handled = ProcessWindowEvent(*copy);

  // What the handlers of the application decided goes back to the notebook,
  // which reads it after sending the event (for example to keep a page open).
  if (copy->IsAllowed()) {
    event.Allow();
  } else {
    event.Veto();
  }
  // Like a handler that did not process the event, it is still unprocessed.
  event.Skip(!handled);
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
  if (page->GetParent() != m_notebook) {
    page->Reparent(m_notebook);
  }
  DoInvalidateBestSize();
  const bool ok =
      m_notebook->InsertPage(pageIdx, page, caption, select, bitmap);
  SyncTabBar();
  return ok;
}

bool GlyphBook::AddPage(wxWindow *page, const wxString &text, bool select,
                        int imageId) {
  return AddPage(page, text, select, GetBitmapBundle(imageId));
}

bool GlyphBook::InsertPage(size_t index, wxWindow *page, const wxString &text,
                           bool select, int imageId) {
  return InsertPage(index, page, text, select, GetBitmapBundle(imageId));
}

bool GlyphBook::DeletePage(size_t page) {
  const bool ok = m_notebook->DeletePage(page);
  DoInvalidateBestSize();
  SyncTabBar();
  return ok;
}

wxWindow *GlyphBook::DoRemovePage(size_t page) {
  if (page >= m_notebook->GetPageCount()) {
    return nullptr;
  }
  wxWindow *const window = m_notebook->GetPage(page);
  if (!m_notebook->RemovePage(page)) {
    return nullptr;
  }
  // The caller owns the page now, so it is not a child of the notebook.
  window->Reparent(this);
  SyncTabBar();
  return window;
}

bool GlyphBook::DeleteAllPages() {
  const bool ok = m_notebook->DeleteAllPages();
  DoInvalidateBestSize();
  SyncTabBar();
  return ok;
}

size_t GlyphBook::GetPageCount() const {
  return m_notebook ? m_notebook->GetPageCount() : 0;
}

wxWindow *GlyphBook::GetPage(size_t pageIdx) const {
  return m_notebook->GetPage(pageIdx);
}

int GlyphBook::FindPage(const wxWindow *page) const {
  return m_notebook ? m_notebook->FindPage(page) : wxNOT_FOUND;
}

bool GlyphBook::SetPageText(size_t page, const wxString &text) {
  const bool ok = m_notebook->SetPageText(page, text);
  SyncTabBar();
  return ok;
}

wxString GlyphBook::GetPageText(size_t pageIdx) const {
  return m_notebook->GetPageText(pageIdx);
}

bool GlyphBook::SetPageToolTip(size_t page, const wxString &text) {
  const bool ok = m_notebook->SetPageToolTip(page, text);
  SyncTabBar();
  return ok;
}

wxString GlyphBook::GetPageToolTip(size_t pageIdx) const {
  return m_notebook->GetPageToolTip(pageIdx);
}

bool GlyphBook::SetPageBitmap(size_t page, const wxBitmapBundle &bitmap) {
  const bool ok = m_notebook->SetPageBitmap(page, bitmap);
  SyncTabBar();
  return ok;
}

wxBitmap GlyphBook::GetPageBitmap(size_t pageIdx) const {
  return m_notebook->GetPageBitmap(pageIdx);
}

bool GlyphBook::SetPageImage(size_t page, int imageId) {
  return SetPageBitmap(page, GetBitmapBundle(imageId));
}

int GlyphBook::GetPageImage(size_t WXUNUSED(page)) const { return wxNOT_FOUND; }

void GlyphBook::SetPageSize(const wxSize &WXUNUSED(size)) {}

int GlyphBook::GetSelection() const {
  return m_notebook ? m_notebook->GetSelection() : wxNOT_FOUND;
}

int GlyphBook::SetSelection(size_t page) {
  const int old = m_notebook->SetSelection(page);
  SyncTabBar();
  return old;
}

int GlyphBook::ChangeSelection(size_t page) {
  const int old = m_notebook->ChangeSelection(page);
  SyncTabBar();
  return old;
}

int GlyphBook::HitTest(const wxPoint &pt, long *flags) const {
  if (flags) {
    *flags = wxBK_HITTEST_NOWHERE;
  }
  if (m_tabBar->IsShown() && m_tabBar->GetRect().Contains(pt)) {
    const int index = m_tabBar->PageIndexAt(pt - m_tabBar->GetPosition());
    if (flags && index != wxNOT_FOUND) {
      *flags = wxBK_HITTEST_ONITEM;
    }
    return index;
  }
  return m_notebook->HitTest(pt - m_notebook->GetPosition(), flags);
}

void GlyphBook::Split(size_t page, int direction) {
  if (!HasVerticalTabs()) {
    m_notebook->Split(page, direction);
  }
}

void GlyphBook::UnsplitAll() { m_notebook->UnsplitAll(); }

void GlyphBook::SaveLayout(const wxString &name,
                           wxAuiBookSerializer &serializer) const {
  m_notebook->SaveLayout(name, serializer);
}

void GlyphBook::LoadLayout(const wxString &name,
                           wxAuiBookDeserializer &deserializer) {
  m_notebook->LoadLayout(name, deserializer);
  if (HasVerticalTabs()) {
    // A saved split cannot be shown by the bar.
    m_notebook->UnsplitAll();
  }
  SyncTabBar();
}

#endif // wxCHECK_VERSION(3, 3, 0)
