#include "GlyphTabBar.h"

#if wxCHECK_VERSION(3, 3, 0)

#include <algorithm>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/popupwin.h>
#include <wx/settings.h>
#include <wx/wupdlock.h>

namespace {

// In DIPs: the limits of the width of the bar, and the width of the edge that
// resizes it.
constexpr int kMinWidth = 80;
constexpr int kMaxWidth = 600;
constexpr int kResizeEdge = 5;

// The icon of the page `index` as the bundle that the notebook has. The bundle
// scales with the display; wxAuiNotebook::GetPageBitmap() gives a bitmap of the
// default size, which does not. A page that is not in the main tab control of
// the notebook (a split notebook) gets that bitmap.
wxBitmapBundle IconOf(wxAuiNotebook *notebook, size_t index) {
  wxWindow *window = notebook->GetPage(index);
  if (wxAuiTabCtrl *ctrl = notebook->GetMainTabCtrl()) {
    for (size_t i = 0; i < ctrl->GetPageCount(); ++i) {
      if (ctrl->GetPage(i).window == window) {
        return ctrl->GetPage(i).bitmap;
      }
    }
  }
  const wxBitmap bitmap = notebook->GetPageBitmap(index);
  return bitmap.IsOk() ? wxBitmapBundle::FromBitmap(bitmap) : wxBitmapBundle{};
}

} // namespace

GlyphTabBar::GlyphTabBar(wxWindow *parent, wxAuiNotebook *notebook, int width)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
               wxFULL_REPAINT_ON_RESIZE | wxBORDER_NONE),
      m_notebook{notebook}, m_width{std::clamp(width, kMinWidth, kMaxWidth)} {
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  SetMinSize(wxSize(FromDIP(m_width), -1));

  Bind(wxEVT_PAINT, &GlyphTabBar::OnPaint, this);
  Bind(wxEVT_SIZE, &GlyphTabBar::OnSize, this);
  Bind(wxEVT_MOTION, &GlyphTabBar::OnMotion, this);
  Bind(wxEVT_LEAVE_WINDOW, &GlyphTabBar::OnLeave, this);
  Bind(wxEVT_LEFT_DOWN, &GlyphTabBar::OnLeftDown, this);
  Bind(wxEVT_LEFT_DCLICK, &GlyphTabBar::OnLeftDown, this);
  Bind(wxEVT_LEFT_UP, &GlyphTabBar::OnLeftUp, this);
  Bind(wxEVT_MIDDLE_UP, &GlyphTabBar::OnMiddleUp, this);
  Bind(wxEVT_RIGHT_UP, &GlyphTabBar::OnRightUp, this);
  Bind(wxEVT_MOUSEWHEEL, &GlyphTabBar::OnWheel, this);
  Bind(wxEVT_MOUSE_CAPTURE_LOST, &GlyphTabBar::OnCaptureLost, this);
  Bind(wxEVT_IDLE, &GlyphTabBar::OnIdle, this);
  Bind(wxEVT_DPI_CHANGED, &GlyphTabBar::OnDpiChanged, this);

  // These are unbound when the bar is destroyed.
  notebook->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED,
                 &GlyphTabBar::OnNotebookChanged, this);
  notebook->Bind(wxEVT_AUINOTEBOOK_PAGE_CLOSED, &GlyphTabBar::OnNotebookChanged,
                 this);
}

void GlyphTabBar::SetArt(std::unique_ptr<GlyphTabArt> art) {
  m_art = std::move(art);
  if (m_art) {
    m_art->SetNormalFont(GetFont());
  }
  LayoutTabs();
  Refresh();
}

void GlyphTabBar::SetSide(wxDirection side) {
  m_side = side == wxRIGHT ? wxRIGHT : wxLEFT;
  Refresh();
}

int GlyphTabBar::ClampWidth(int width) const {
  width = std::clamp(width, kMinWidth, kMaxWidth);
  // Keep half of the parent for the notebook.
  if (GetParent()) {
    const int half = ToDIP(GetParent()->GetClientSize().x / 2);
    if (half >= kMinWidth) {
      width = std::min(width, half);
    }
  }
  return width;
}

void GlyphTabBar::MoveGuide() {
  // In screen coordinates: the bar, and where its inner edge will be.
  const wxPoint origin = ClientToScreen(wxPoint(0, 0));
  const wxSize size = GetSize();
  const int lineWidth = std::max(2, FromDIP(2));
  const int edge = GetInnerSide() == wxRIGHT
                       ? origin.x + FromDIP(m_dragWidth)
                       : origin.x + size.x - FromDIP(m_dragWidth);
  if (!m_guide) {
    // A popup window is above all the windows of the frame, also above the
    // native window of the terminal, and it does not take the focus.
    m_guide = new wxPopupWindow(this, wxBORDER_NONE);
    m_guide->SetBackgroundColour(
        wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT));
  }
  m_guide->SetSize(edge - lineWidth / 2, origin.y, lineWidth, size.y);
  if (!m_guide->IsShown()) {
    m_guide->Show();
  }
}

void GlyphTabBar::SetBarWidth(int width) {
  width = ClampWidth(width);
  if (width == m_width && GetMinSize().x == FromDIP(width)) {
    return;
  }
  m_width = width;
  SetMinSize(wxSize(FromDIP(m_width), -1));

  // A drag sends many mouse events. A layout resizes the notebook, the pages
  // and the terminals, which is slow and flickers when it runs for each event.
  // So run one layout after the events that are waiting, with the parent
  // frozen so the changes are shown together.
  if (GetParent() && !m_layoutPending) {
    m_layoutPending = true;
    CallAfter([this] {
      m_layoutPending = false;
      if (wxWindow *parent = GetParent()) {
        wxWindowUpdateLocker locker(parent);
        parent->Layout();
      }
    });
  }
}

void GlyphTabBar::SyncWithNotebook() {
  if (!m_notebook) {
    return;
  }

  const long style = m_notebook->GetWindowStyleFlag();
  const int selection = m_notebook->GetSelection();
  // The order of the main tab control, unless some pages are in another tab
  // control (a split notebook): then the order of the notebook.
  std::vector<size_t> order;
  if (wxAuiTabCtrl *ctrl = m_notebook->GetMainTabCtrl()) {
    order = m_notebook->GetPagesInDisplayOrder(ctrl);
  }
  if (order.size() != m_notebook->GetPageCount()) {
    order.resize(m_notebook->GetPageCount());
    for (size_t i = 0; i < order.size(); ++i) {
      order[i] = i;
    }
  }

  std::vector<Tab> tabs;
  tabs.reserve(order.size());
  for (const size_t index : order) {
    Tab tab;
    tab.index = index;
    tab.page.window = m_notebook->GetPage(index);
    tab.page.caption = m_notebook->GetPageText(index);
    tab.page.tooltip = m_notebook->GetPageToolTip(index);
    const wxBitmapBundle bitmap = IconOf(m_notebook, index);
    if (bitmap.IsOk()) {
      tab.page.bitmap = bitmap;
    }
    tab.page.active = static_cast<int>(index) == selection;
    if ((style & wxAUI_NB_CLOSE_ON_ALL_TABS) ||
        (tab.page.active && (style & wxAUI_NB_CLOSE_ON_ACTIVE_TAB))) {
      tab.page.buttons.emplace_back(wxAUI_BUTTON_CLOSE, wxRIGHT);
    }
    tabs.push_back(std::move(tab));
  }

  const auto same = [](const Tab &a, const Tab &b) {
    return a.index == b.index && a.page.window == b.page.window &&
           a.page.caption == b.page.caption &&
           a.page.tooltip == b.page.tooltip && a.page.active == b.page.active &&
           a.page.bitmap.IsOk() == b.page.bitmap.IsOk() &&
           a.page.buttons.size() == b.page.buttons.size();
  };

  if (std::equal(tabs.begin(), tabs.end(), m_tabs.begin(), m_tabs.end(),
                 same)) {
    // Same tabs, but an icon can be another bitmap (a spinner).
    for (size_t i = 0; i < tabs.size(); ++i) {
      m_tabs[i].page.bitmap = tabs[i].page.bitmap;
    }
    Refresh();
    return;
  }

  const auto findActive = [](const std::vector<Tab> &list) -> wxWindow * {
    for (const auto &tab : list) {
      if (tab.page.active) {
        return tab.page.window;
      }
    }
    return nullptr;
  };
  const bool activeChanged = findActive(tabs) != findActive(m_tabs);
  m_tabs = std::move(tabs);

  // Forget the pages that are gone.
  const auto exists = [this](wxWindow *window) {
    return std::any_of(m_tabs.begin(), m_tabs.end(), [window](const Tab &tab) {
      return tab.page.window == window;
    });
  };
  for (wxWindow **window :
       {&m_hover, &m_closeHover, &m_closePressed, &m_dragged}) {
    if (*window && !exists(*window)) {
      *window = nullptr;
    }
  }

  LayoutTabs();
  if (activeChanged) {
    // Show the current tab.
    for (const auto &tab : m_tabs) {
      if (!tab.page.active) {
        continue;
      }
      const wxRect &rect = tab.page.rect;
      const int height = GetClientSize().y;
      if (rect.y < 0) {
        ScrollBy(rect.y);
      } else if (rect.GetBottom() >= height && rect.height <= height) {
        ScrollBy(rect.GetBottom() - height + 1);
      }
      break;
    }
  }
  Refresh();
}

void GlyphTabBar::LayoutTabs() {
  if (!m_art) {
    return;
  }
  wxInfoDC dc(this);
  const wxSize client = GetClientSize();

  m_contentHeight = 0;
  std::vector<int> heights;
  heights.reserve(m_tabs.size());
  for (const auto &tab : m_tabs) {
    heights.push_back(m_art->GetVerticalTabHeight(dc, this, tab.page));
    m_contentHeight += heights.back();
  }
  m_scroll = std::clamp(m_scroll, 0, std::max(0, m_contentHeight - client.y));

  int y = -m_scroll;
  for (size_t i = 0; i < m_tabs.size(); ++i) {
    m_tabs[i].page.rect = wxRect(0, y, client.x, heights[i]);
    y += heights[i];
  }
}

void GlyphTabBar::OnPaint(wxPaintEvent &WXUNUSED(event)) {
  wxAutoBufferedPaintDC dc(this);
  const wxRect client = GetClientRect();
  if (!m_art) {
    dc.SetBackground(GetBackgroundColour());
    dc.Clear();
    return;
  }

  m_art->DrawVerticalBackground(dc, this, client, GetInnerSide());
  for (auto &tab : m_tabs) {
    wxWindow *const window = tab.page.window;
    tab.page.hover = window == m_hover;
    for (auto &button : tab.page.buttons) {
      button.curState = wxAUI_BUTTON_STATE_NORMAL;
      if (window == m_closeHover) {
        button.curState |= wxAUI_BUTTON_STATE_HOVER;
        if (window == m_closePressed) {
          button.curState |= wxAUI_BUTTON_STATE_PRESSED;
        }
      }
    }
    if (!tab.page.rect.Intersects(client)) {
      // Not painted, so the buttons cannot be clicked.
      for (auto &button : tab.page.buttons) {
        button.rect = wxRect();
      }
      continue;
    }
    m_art->DrawVerticalTab(dc, this, tab.page, tab.page.rect, GetInnerSide());
  }
}

void GlyphTabBar::OnSize(wxSizeEvent &event) {
  event.Skip();
  LayoutTabs();
  Refresh();
}

GlyphTabBar::Tab *GlyphTabBar::TabAt(const wxPoint &pt) {
  for (auto &tab : m_tabs) {
    if (tab.page.rect.Contains(pt)) {
      return &tab;
    }
  }
  return nullptr;
}

int GlyphTabBar::PageIndexAt(const wxPoint &pt) const {
  for (const auto &tab : m_tabs) {
    if (tab.page.rect.Contains(pt)) {
      return static_cast<int>(tab.index);
    }
  }
  return wxNOT_FOUND;
}

bool GlyphTabBar::IsOnCloseButton(const Tab &tab, const wxPoint &pt) const {
  return std::any_of(tab.page.buttons.begin(), tab.page.buttons.end(),
                     [&pt](const wxAuiTabContainerButton &button) {
                       return button.id == wxAUI_BUTTON_CLOSE &&
                              button.rect.Contains(pt);
                     });
}

bool GlyphTabBar::IsOnResizeEdge(const wxPoint &pt) const {
  const int edge = FromDIP(kResizeEdge);
  return GetInnerSide() == wxRIGHT ? pt.x >= GetClientSize().x - edge
                                   : pt.x < edge;
}

bool GlyphTabBar::UpdateHover(const wxPoint &pt) {
  wxWindow *hover = nullptr;
  wxWindow *closeHover = nullptr;
  const bool inside = GetClientRect().Contains(pt);
  if (Tab *tab = inside ? TabAt(pt) : nullptr) {
    hover = tab->page.window;
    if (IsOnCloseButton(*tab, pt)) {
      closeHover = hover;
    }
  }
  if (hover == m_hover && closeHover == m_closeHover) {
    return false;
  }
  if (hover != m_hover) {
    // The tooltip of the page, or the whole label, for a label that does not
    // fit.
    Tab *tab = hover ? TabAt(pt) : nullptr;
    if (tab) {
      SetToolTip(tab->page.tooltip.empty() ? tab->page.caption
                                           : tab->page.tooltip);
    } else {
      UnsetToolTip();
    }
  }
  m_hover = hover;
  m_closeHover = closeHover;
  return true;
}

void GlyphTabBar::OnMotion(wxMouseEvent &event) {
  const wxPoint pt = event.GetPosition();
  if (m_resizing) {
    const int dx = ClientToScreen(pt).x - m_resizeStartX;
    const int width =
        m_resizeStartWidth + (GetInnerSide() == wxRIGHT ? dx : -dx);
    // Only a guide line moves. The layout (which resizes the terminals) runs
    // once, when the mouse is released.
    m_dragWidth = ClampWidth(ToDIP(width));
    MoveGuide();
    return;
  }
  if (m_dragged) {
    DragTo(pt);
    return;
  }
  if (m_closePressed) {
    // The button looks pressed only while the mouse is on it.
    Tab *tab = TabAt(pt);
    wxWindow *closeHover =
        tab && tab->page.window == m_closePressed && IsOnCloseButton(*tab, pt)
            ? m_closePressed
            : nullptr;
    if (closeHover != m_closeHover) {
      m_closeHover = closeHover;
      Refresh();
    }
    return;
  }

  SetCursor(IsOnResizeEdge(pt) ? wxCursor(wxCURSOR_SIZEWE) : wxNullCursor);
  if (UpdateHover(pt)) {
    Refresh();
  }
}

void GlyphTabBar::OnLeave(wxMouseEvent &WXUNUSED(event)) {
  if (HasCapture()) {
    return;
  }
  if (UpdateHover(wxPoint(-1, -1))) {
    Refresh();
  }
}

void GlyphTabBar::OnLeftDown(wxMouseEvent &event) {
  // A double click is a second press: the first one may still hold the mouse.
  if (!m_notebook || HasCapture()) {
    return;
  }
  const wxPoint pt = event.GetPosition();
  if (IsOnResizeEdge(pt)) {
    m_resizing = true;
    m_resizeStartX = ClientToScreen(pt).x;
    m_resizeStartWidth = GetSize().x;
    m_dragWidth = m_width;
    CaptureMouse();
    return;
  }

  Tab *tab = TabAt(pt);
  if (!tab) {
    return;
  }
  if (IsOnCloseButton(*tab, pt)) {
    m_closePressed = m_closeHover = tab->page.window;
    CaptureMouse();
    Refresh();
    return;
  }

  // Copies: selecting the page reads the pages again, which replaces the tab.
  wxWindow *const window = tab->page.window;
  const size_t index = tab->index;
  m_dragged = window;
  CaptureMouse();
  if (static_cast<int>(index) != m_notebook->GetSelection()) {
    m_notebook->SetSelection(index);
  } else {
    window->SetFocus();
  }
}

void GlyphTabBar::OnLeftUp(wxMouseEvent &event) {
  wxWindow *const closed = m_closePressed;
  const bool onButton = closed && m_closeHover == closed;
  EndMouseAction();
  if (onButton) {
    for (const auto &tab : m_tabs) {
      if (tab.page.window == closed) {
        ClosePage(tab.index);
        break;
      }
    }
  }
  if (UpdateHover(event.GetPosition())) {
    Refresh();
  }
}

void GlyphTabBar::OnMiddleUp(wxMouseEvent &event) {
  Tab *tab = TabAt(event.GetPosition());
  if (!m_notebook || !tab) {
    return;
  }
  const size_t index = tab->index;
  // Like the notebook: the application can handle the click itself.
  wxAuiNotebookEvent e(wxEVT_AUINOTEBOOK_TAB_MIDDLE_UP, m_notebook->GetId());
  e.SetSelection(static_cast<int>(index));
  e.SetEventObject(m_notebook);
  if (m_notebook->ProcessWindowEvent(e)) {
    return;
  }
  if (m_notebook->GetWindowStyleFlag() & wxAUI_NB_MIDDLE_CLICK_CLOSE) {
    ClosePage(index);
  }
}

void GlyphTabBar::OnRightUp(wxMouseEvent &event) {
  Tab *tab = TabAt(event.GetPosition());
  if (!m_notebook || !tab) {
    return;
  }
  wxAuiNotebookEvent e(wxEVT_AUINOTEBOOK_TAB_RIGHT_UP, m_notebook->GetId());
  e.SetSelection(static_cast<int>(tab->index));
  e.SetEventObject(m_notebook);
  m_notebook->ProcessWindowEvent(e);
}

void GlyphTabBar::OnWheel(wxMouseEvent &event) {
  const int rows =
      event.GetWheelRotation() / std::max(1, event.GetWheelDelta());
  const int rowHeight =
      m_tabs.empty() ? FromDIP(24) : m_tabs.front().page.rect.height;
  ScrollBy(-rows * rowHeight);
  if (UpdateHover(event.GetPosition())) {
    Refresh();
  }
}

void GlyphTabBar::ScrollBy(int dy) {
  const int old = m_scroll;
  m_scroll += dy;
  LayoutTabs();
  if (m_scroll != old) {
    Refresh();
  }
}

void GlyphTabBar::DragTo(const wxPoint &pt) {
  if (!m_notebook || m_tabs.empty()) {
    return;
  }
  // The row under the mouse; above the first row or below the last one, the
  // first or the last row.
  size_t target = m_tabs.size() - 1;
  if (pt.y < m_tabs.front().page.rect.y) {
    target = 0;
  } else {
    for (size_t i = 0; i < m_tabs.size(); ++i) {
      if (pt.y < m_tabs[i].page.rect.GetBottom() + 1) {
        target = i;
        break;
      }
    }
  }
  const auto current =
      std::find_if(m_tabs.begin(), m_tabs.end(), [this](const Tab &tab) {
        return tab.page.window == m_dragged;
      });
  if (current == m_tabs.end() ||
      static_cast<size_t>(current - m_tabs.begin()) == target) {
    return;
  }

  // Only the order of the main tab control can be changed, and only when it
  // has all the pages (see SyncWithNotebook()).
  wxAuiTabCtrl *ctrl = m_notebook->GetMainTabCtrl();
  if (!ctrl || ctrl->GetPageCount() != m_tabs.size() ||
      !ctrl->MovePage(m_dragged, target)) {
    return;
  }
  ctrl->Refresh();
  SyncWithNotebook();
}

void GlyphTabBar::ClosePage(size_t index) {
  if (!m_notebook || index >= m_notebook->GetPageCount()) {
    return;
  }
  // The same events as the close button of the notebook.
  wxAuiNotebookEvent e(wxEVT_AUINOTEBOOK_PAGE_CLOSE, m_notebook->GetId());
  e.SetSelection(static_cast<int>(index));
  e.SetEventObject(m_notebook);
  m_notebook->ProcessWindowEvent(e);
  if (!e.IsAllowed()) {
    return;
  }
  m_notebook->DeletePage(index);

  wxAuiNotebookEvent closed(wxEVT_AUINOTEBOOK_PAGE_CLOSED, m_notebook->GetId());
  closed.SetSelection(static_cast<int>(index));
  closed.SetEventObject(m_notebook);
  m_notebook->ProcessWindowEvent(closed);
  SyncWithNotebook();
}

void GlyphTabBar::EndMouseAction() {
  const bool resized = m_resizing;
  if (HasCapture()) {
    ReleaseMouse();
  }
  m_resizing = false;
  if (m_guide) {
    m_guide->Destroy();
    m_guide = nullptr;
  }
  if (resized) {
    SetBarWidth(m_dragWidth);
  }
  m_dragged = nullptr;
  m_closePressed = nullptr;
  Refresh();
  if (resized && m_onWidthChanged) {
    m_onWidthChanged(m_width);
  }
}

void GlyphTabBar::OnCaptureLost(wxMouseCaptureLostEvent &WXUNUSED(event)) {
  EndMouseAction();
}

void GlyphTabBar::OnIdle(wxIdleEvent &event) {
  event.Skip();
  if (IsShownOnScreen()) {
    SyncWithNotebook();
  }
}

void GlyphTabBar::OnDpiChanged(wxDPIChangedEvent &event) {
  event.Skip();
  SetMinSize(wxSize(FromDIP(m_width), -1));
  LayoutTabs();
  Refresh();
}

void GlyphTabBar::OnNotebookChanged(wxAuiNotebookEvent &event) {
  event.Skip();
  if (IsShown()) {
    SyncWithNotebook();
  }
}

#endif // wxCHECK_VERSION(3, 3, 0)
