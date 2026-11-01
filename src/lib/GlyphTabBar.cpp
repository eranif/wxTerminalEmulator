#include "GlyphTabBar.h"

#include <algorithm>
#include <cstdlib>
#include <wx/dcbuffer.h>
#include <wx/dcclient.h>
#include <wx/menu.h>
#include <wx/popupwin.h>
#include <wx/settings.h>
#include <wx/utils.h>
#include <wx/wupdlock.h>

namespace {

// In DIPs: the limits of the width of a vertical bar, and the width of the edge
// that resizes it.
constexpr int kMinWidth = 80;
constexpr int kMaxWidth = 600;
constexpr int kResizeEdge = 5;
// The narrowest a tab of a horizontal bar gets before the bar scrolls.
constexpr int kMinTabWidth = 72;
// The width of the button with the list of tabs.
constexpr int kListWidth = 28;
// How far the mouse must go, in DIPs, before a tab is dragged.
constexpr int kDragDistance = 5;
// How far one turn of the wheel scrolls a horizontal bar, in DIPs.
constexpr int kWheelStep = 60;

// Make the widths fit in @p room: a tab narrower than its fair share keeps its
// width, the others share the rest, but none gets narrower than @p minWidth
// (or than it was).
void ShrinkWidths(std::vector<int> &widths, int room, int minWidth) {
  std::vector<bool> fixed(widths.size(), false);
  size_t flexible = widths.size();
  int remaining = room;
  for (bool changed = true; changed && flexible;) {
    changed = false;
    const int share = remaining / static_cast<int>(flexible);
    for (size_t i = 0; i < widths.size(); ++i) {
      if (!fixed[i] && widths[i] <= share) {
        fixed[i] = true;
        remaining -= widths[i];
        --flexible;
        changed = true;
      }
    }
  }
  if (!flexible) {
    return;
  }
  const int share = remaining / static_cast<int>(flexible);
  for (size_t i = 0; i < widths.size(); ++i) {
    if (!fixed[i]) {
      widths[i] = std::max(share, std::min(widths[i], minWidth));
    }
  }
}

} // namespace

GlyphTabBar::GlyphTabBar(wxWindow *parent, int width)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
               wxFULL_REPAINT_ON_RESIZE | wxBORDER_NONE),
      m_width{std::clamp(width, kMinWidth, kMaxWidth)} {
  SetBackgroundStyle(wxBG_STYLE_PAINT);

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
  Bind(wxEVT_DPI_CHANGED, &GlyphTabBar::OnDpiChanged, this);
}

void GlyphTabBar::SetPainter(std::unique_ptr<GlyphTabPainter> painter) {
  m_painter = std::move(painter);
  if (m_painter) {
    m_painter->SetFont(GetFont());
  }
  UpdateMinSize();
  LayoutTabs();
  Refresh();
}

void GlyphTabBar::SetSide(wxDirection side) {
  m_side =
      (side == wxLEFT || side == wxRIGHT || side == wxBOTTOM) ? side : wxTOP;
  m_scroll = 0;
  SetCursor(wxNullCursor);
  UpdateMinSize();
  LayoutTabs();
  Refresh();
}

wxDirection GlyphTabBar::GetInnerSide() const {
  switch (m_side) {
  case wxLEFT:
    return wxRIGHT;
  case wxRIGHT:
    return wxLEFT;
  case wxBOTTOM:
    return wxTOP;
  default:
    return wxBOTTOM;
  }
}

void GlyphTabBar::SetCloseButtons(bool onAllTabs, bool onActiveTab) {
  m_closeOnAll = onAllTabs;
  m_closeOnActive = onActiveTab;
  UpdateButtons();
  LayoutTabs();
  Refresh();
}

int GlyphTabBar::ClampWidth(int width) const {
  width = std::clamp(width, kMinWidth, kMaxWidth);
  // Keep half of the parent for the pages.
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

void GlyphTabBar::RequestParentLayout() {
  // A layout resizes the pages and the terminals, which is slow and flickers
  // when it runs for each mouse event of a drag. So run one layout after the
  // events that are waiting, with the parent frozen so the changes are shown
  // together.
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

void GlyphTabBar::SetBarWidth(int width) {
  width = ClampWidth(width);
  if (width == m_width && GetMinSize().x == FromDIP(width)) {
    return;
  }
  m_width = width;
  if (IsVertical()) {
    SetMinSize(wxSize(FromDIP(m_width), -1));
    RequestParentLayout();
  }
}

void GlyphTabBar::UpdateMinSize() {
  if (IsVertical()) {
    SetMinSize(wxSize(FromDIP(m_width), -1));
    return;
  }
  // The height of the tallest tab (the icons can differ). Without tabs, the
  // height of a tab with a label.
  int height = 0;
  if (m_painter) {
    GlyphTabInfo probe;
    probe.caption = wxS("Xg");
    height = m_painter->GetTabSize(this, probe).y;
    for (const auto &tab : m_tabs) {
      height = std::max(height, m_painter->GetTabSize(this, tab.info).y);
    }
  }
  const wxSize minSize(-1, height);
  if (GetMinSize() != minSize) {
    SetMinSize(minSize);
    RequestParentLayout();
  }
}

// Tabs
// ----

int GlyphTabBar::Find(const wxWindow *window) const {
  for (size_t i = 0; i < m_tabs.size(); ++i) {
    if (m_tabs[i].window == window) {
      return static_cast<int>(i);
    }
  }
  return wxNOT_FOUND;
}

void GlyphTabBar::UpdateButtons() {
  for (size_t i = 0; i < m_tabs.size(); ++i) {
    GlyphTabInfo &info = m_tabs[i].info;
    info.active = static_cast<int>(i) == m_selection;
    info.buttons.clear();
    if (m_closeOnAll || (m_closeOnActive && info.active)) {
      GlyphTabButton close;
      close.close = true;
      info.buttons.push_back(close);
    }
  }
}

void GlyphTabBar::Insert(size_t index, wxWindow *window,
                         const wxString &caption,
                         const wxBitmapBundle &bitmap) {
  Tab tab;
  tab.window = window;
  tab.info.caption = caption;
  tab.info.bitmap = bitmap;
  index = std::min(index, m_tabs.size());
  m_dropIndex = -1;
  m_tabs.insert(m_tabs.begin() + index, std::move(tab));
  if (m_selection >= 0 && static_cast<int>(index) <= m_selection) {
    ++m_selection;
  }
  UpdateButtons();
  UpdateMinSize();
  LayoutTabs();
  Refresh();
}

void GlyphTabBar::Remove(size_t index) {
  if (index >= m_tabs.size()) {
    return;
  }
  wxWindow *const window = m_tabs[index].window;
  // The tabs change, so the place of a drop that is shown is not valid.
  m_dropIndex = -1;
  for (wxWindow **state :
       {&m_hover, &m_closeHover, &m_closePressed, &m_dragged}) {
    if (*state == window) {
      *state = nullptr;
    }
  }
  m_tabs.erase(m_tabs.begin() + index);
  if (static_cast<int>(index) < m_selection) {
    --m_selection;
  } else if (static_cast<int>(index) == m_selection) {
    m_selection = -1;
  }
  UpdateButtons();
  UpdateMinSize();
  LayoutTabs();
  Refresh();
}

void GlyphTabBar::Clear() {
  m_tabs.clear();
  m_dropIndex = -1;
  m_selection = -1;
  m_hover = m_closeHover = m_closePressed = m_dragged = nullptr;
  m_scroll = 0;
  UpdateMinSize();
  LayoutTabs();
  Refresh();
}

void GlyphTabBar::Move(size_t from, size_t to) {
  if (from >= m_tabs.size() || to >= m_tabs.size() || from == to) {
    return;
  }
  wxWindow *const selected =
      m_selection >= 0 ? m_tabs[m_selection].window : nullptr;
  m_dropIndex = -1;
  Tab tab = std::move(m_tabs[from]);
  m_tabs.erase(m_tabs.begin() + from);
  m_tabs.insert(m_tabs.begin() + to, std::move(tab));
  m_selection = selected ? Find(selected) : -1;
  LayoutTabs();
  Refresh();
}

void GlyphTabBar::SetCaption(size_t index, const wxString &caption) {
  if (index < m_tabs.size() && m_tabs[index].info.caption != caption) {
    m_tabs[index].info.caption = caption;
    LayoutTabs();
    Refresh();
  }
}

void GlyphTabBar::SetTooltip(size_t index, const wxString &tooltip) {
  if (index < m_tabs.size()) {
    m_tabs[index].info.tooltip = tooltip;
  }
}

void GlyphTabBar::SetBitmap(size_t index, const wxBitmapBundle &bitmap) {
  if (index < m_tabs.size()) {
    m_tabs[index].info.bitmap = bitmap;
    UpdateMinSize();
    LayoutTabs();
    Refresh();
  }
}

void GlyphTabBar::SetSelection(int index) {
  if (index < -1 || index >= static_cast<int>(m_tabs.size())) {
    return;
  }
  m_selection = index;
  UpdateButtons();
  LayoutTabs();
  if (index >= 0) {
    EnsureVisible(static_cast<size_t>(index));
  }
  Refresh();
}

int GlyphTabBar::HitTest(const wxPoint &pt) const { return TabIndexAt(pt); }

// Layout
// ------

void GlyphTabBar::LayoutTabs() {
  if (!m_painter) {
    return;
  }
  const wxSize client = GetClientSize();
  m_listRect = wxRect();

  if (IsVertical()) {
    // Rows from the top.
    m_tabsRoom = client.y;
    std::vector<int> heights;
    heights.reserve(m_tabs.size());
    m_contentSize = 0;
    for (const auto &tab : m_tabs) {
      heights.push_back(m_painter->GetVerticalTabHeight(this, tab.info));
      m_contentSize += heights.back();
    }
    m_scroll = std::clamp(m_scroll, 0, std::max(0, m_contentSize - client.y));
    int y = -m_scroll;
    for (size_t i = 0; i < m_tabs.size(); ++i) {
      m_tabs[i].info.rect = wxRect(0, y, client.x, heights[i]);
      y += heights[i];
    }
    return;
  }

  // Tabs from the left. They get narrower when they do not fit; when they are
  // as narrow as they can be and still do not fit, the bar scrolls and has the
  // list button.
  std::vector<int> widths;
  widths.reserve(m_tabs.size());
  int total = 0;
  for (const auto &tab : m_tabs) {
    widths.push_back(m_painter->GetTabSize(this, tab.info).x);
    total += widths.back();
  }
  if (total > client.x && !m_tabs.empty()) {
    ShrinkWidths(widths, client.x, FromDIP(kMinTabWidth));
    total = 0;
    for (const int width : widths) {
      total += width;
    }
  }
  m_tabsRoom = client.x;
  if (total > client.x) {
    const int listWidth = FromDIP(kListWidth);
    m_tabsRoom = std::max(0, client.x - listWidth);
    m_listRect = wxRect(m_tabsRoom, 0, client.x - m_tabsRoom, client.y);
  }
  m_contentSize = total;
  m_scroll = std::clamp(m_scroll, 0, std::max(0, total - m_tabsRoom));
  int x = -m_scroll;
  for (size_t i = 0; i < m_tabs.size(); ++i) {
    m_tabs[i].info.rect = wxRect(x, 0, widths[i], client.y);
    x += widths[i];
  }
}

void GlyphTabBar::ScrollBy(int delta) {
  const int old = m_scroll;
  m_scroll += delta;
  LayoutTabs();
  if (m_scroll != old) {
    Refresh();
  }
}

void GlyphTabBar::EnsureVisible(size_t index) {
  // The bar has no size yet: OnSize() shows the tab when it has one.
  if (index >= m_tabs.size() || m_tabsRoom <= 0) {
    return;
  }
  const wxRect &rect = m_tabs[index].info.rect;
  const int start = IsVertical() ? rect.y : rect.x;
  const int length = IsVertical() ? rect.height : rect.width;
  if (start < 0) {
    ScrollBy(start);
  } else if (start + length > m_tabsRoom) {
    // A tab longer than the room is shown from its start.
    ScrollBy(length <= m_tabsRoom ? start + length - m_tabsRoom : start);
  }
}

// Painting
// --------

void GlyphTabBar::OnPaint(wxPaintEvent &WXUNUSED(event)) {
  wxAutoBufferedPaintDC dc(this);
  const wxRect client = GetClientRect();
  if (!m_painter) {
    dc.SetBackground(GetBackgroundColour());
    dc.Clear();
    return;
  }

  const wxDirection inner = GetInnerSide();
  m_painter->DrawBarBackground(dc, this, client, inner);
  for (auto &tab : m_tabs) {
    GlyphTabInfo &info = tab.info;
    info.hover = tab.window == m_hover;
    for (auto &button : info.buttons) {
      button.state = 0;
      if (tab.window == m_closeHover) {
        button.state |= GlyphTabButton::kHover;
        if (tab.window == m_closePressed) {
          button.state |= GlyphTabButton::kPressed;
        }
      }
    }
    // The tabs that are not in the window are not painted, so their buttons
    // cannot be clicked.
    const wxRect rect = info.rect;
    if (!rect.Intersects(client) ||
        (!m_listRect.IsEmpty() && rect.x >= m_tabsRoom)) {
      for (auto &button : info.buttons) {
        button.rect = wxRect();
      }
      continue;
    }
    m_painter->DrawTab(dc, this, info, rect, inner);
  }

  if (m_dragged && m_dropIndex != wxNOT_FOUND) {
    DrawDropLine(dc);
  }

  if (!m_listRect.IsEmpty()) {
    // The list button covers the end of the tabs that go under it.
    m_painter->DrawBarBackground(dc, this, m_listRect, inner);
    m_painter->DrawDropDownButton(dc, this, m_listRect, m_listHover);
  }
}

void GlyphTabBar::OnSize(wxSizeEvent &event) {
  event.Skip();
  LayoutTabs();
  // The selected tab can be off the bar after a resize, and was not shown at
  // all if it was selected before the bar had a size (a restored layout). Not
  // while the mouse is held: the user is dragging.
  if (m_selection >= 0 && !HasCapture()) {
    EnsureVisible(static_cast<size_t>(m_selection));
  }
  Refresh();
}

void GlyphTabBar::OnDpiChanged(wxDPIChangedEvent &event) {
  event.Skip();
  UpdateMinSize();
  LayoutTabs();
  Refresh();
}

// Mouse
// -----

int GlyphTabBar::TabIndexAt(const wxPoint &pt) const {
  if (!m_listRect.IsEmpty() && pt.x >= m_tabsRoom) {
    return wxNOT_FOUND;
  }
  for (size_t i = 0; i < m_tabs.size(); ++i) {
    if (m_tabs[i].info.rect.Contains(pt)) {
      return static_cast<int>(i);
    }
  }
  return wxNOT_FOUND;
}

bool GlyphTabBar::IsOnCloseButton(const Tab &tab, const wxPoint &pt) const {
  return std::any_of(tab.info.buttons.begin(), tab.info.buttons.end(),
                     [&pt](const GlyphTabButton &button) {
                       return button.close && button.rect.Contains(pt);
                     });
}

bool GlyphTabBar::IsOnResizeEdge(const wxPoint &pt) const {
  if (!IsVertical()) {
    return false;
  }
  const int edge = FromDIP(kResizeEdge);
  return GetInnerSide() == wxRIGHT ? pt.x >= GetClientSize().x - edge
                                   : pt.x < edge;
}

bool GlyphTabBar::UpdateHover(const wxPoint &pt) {
  wxWindow *hover = nullptr;
  wxWindow *closeHover = nullptr;
  const bool inside = GetClientRect().Contains(pt);
  const int index = inside ? TabIndexAt(pt) : wxNOT_FOUND;
  if (index != wxNOT_FOUND) {
    hover = m_tabs[index].window;
    if (IsOnCloseButton(m_tabs[index], pt)) {
      closeHover = hover;
    }
  }
  const bool listHover = inside && m_listRect.Contains(pt);
  if (hover == m_hover && closeHover == m_closeHover &&
      listHover == m_listHover) {
    return false;
  }
  if (hover != m_hover) {
    // The tooltip of the page, or the whole label, for a label that does not
    // fit.
    if (index != wxNOT_FOUND) {
      const GlyphTabInfo &info = m_tabs[index].info;
      SetToolTip(info.tooltip.empty() ? info.caption : info.tooltip);
    } else {
      UnsetToolTip();
    }
  }
  m_hover = hover;
  m_closeHover = closeHover;
  m_listHover = listHover;
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
    if (!m_dragStarted) {
      const wxPoint delta = pt - m_pressPoint;
      m_dragStarted =
          std::abs(delta.x) + std::abs(delta.y) >= FromDIP(kDragDistance);
    }
    if (m_dragStarted) {
      DragTo(pt);
    }
    return;
  }
  if (m_closePressed) {
    // The button looks pressed only while the mouse is on it.
    const int index = TabIndexAt(pt);
    wxWindow *closeHover = index != wxNOT_FOUND &&
                                   m_tabs[index].window == m_closePressed &&
                                   IsOnCloseButton(m_tabs[index], pt)
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
  if (HasCapture()) {
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
  if (!m_listRect.IsEmpty() && m_listRect.Contains(pt)) {
    // The menu runs its own loop: show it after this event.
    CallAfter(&GlyphTabBar::ShowTabList);
    return;
  }

  const int index = TabIndexAt(pt);
  if (index == wxNOT_FOUND) {
    return;
  }
  if (IsOnCloseButton(m_tabs[index], pt)) {
    m_closePressed = m_closeHover = m_tabs[index].window;
    CaptureMouse();
    Refresh();
    return;
  }

  m_dragged = m_tabs[index].window;
  m_pressPoint = pt;
  m_dragStarted = false;
  CaptureMouse();
  if (m_callbacks.select) {
    m_callbacks.select(static_cast<size_t>(index));
  }
}

void GlyphTabBar::OnLeftUp(wxMouseEvent &event) {
  wxWindow *const closed = m_closePressed;
  const bool onButton = closed && m_closeHover == closed;
  wxWindow *const dragged = m_dragged;
  const int drop = m_dropIndex;
  EndMouseAction();
  if (dragged && drop != wxNOT_FOUND) {
    // The gap is counted before the tab is taken out of the list.
    const int from = Find(dragged);
    if (from != wxNOT_FOUND) {
      Move(static_cast<size_t>(from),
           static_cast<size_t>(drop > from ? drop - 1 : drop));
    }
  }
  if (onButton) {
    const int index = Find(closed);
    if (index != wxNOT_FOUND && m_callbacks.close) {
      m_callbacks.close(static_cast<size_t>(index));
    }
  }
  if (UpdateHover(event.GetPosition())) {
    Refresh();
  }
}

void GlyphTabBar::OnMiddleUp(wxMouseEvent &event) {
  const int index = TabIndexAt(event.GetPosition());
  if (index != wxNOT_FOUND && m_callbacks.middleUp) {
    m_callbacks.middleUp(static_cast<size_t>(index));
  }
}

void GlyphTabBar::OnRightUp(wxMouseEvent &event) {
  const int index = TabIndexAt(event.GetPosition());
  if (index != wxNOT_FOUND && m_callbacks.rightUp) {
    m_callbacks.rightUp(static_cast<size_t>(index));
  }
}

void GlyphTabBar::OnWheel(wxMouseEvent &event) {
  // A small rotation (a touchpad) adds up until it makes a row.
  const int rotation = event.GetWheelRotation();
  if ((rotation > 0) != (m_wheelRemainder > 0)) {
    m_wheelRemainder = 0;
  }
  m_wheelRemainder += rotation;
  const int delta = std::max(1, event.GetWheelDelta());
  const int rows = m_wheelRemainder / delta;
  m_wheelRemainder -= rows * delta;
  int step = FromDIP(kWheelStep);
  if (IsVertical()) {
    step = m_tabs.empty() ? FromDIP(24) : m_tabs.front().info.rect.height;
  }
  ScrollBy(-rows * step);
  if (UpdateHover(event.GetPosition())) {
    Refresh();
  }
}

void GlyphTabBar::DragTo(const wxPoint &pt) {
  const int current = Find(m_dragged);
  if (current == wxNOT_FOUND || m_tabs.empty()) {
    return;
  }
  // The gap under the mouse: before the first tab whose middle is after the
  // mouse, or after the last tab.
  const int position = IsVertical() ? pt.y : pt.x;
  int gap = static_cast<int>(m_tabs.size());
  for (size_t i = 0; i < m_tabs.size(); ++i) {
    const wxRect &rect = m_tabs[i].info.rect;
    const int middle =
        IsVertical() ? rect.y + rect.height / 2 : rect.x + rect.width / 2;
    if (position < middle) {
      gap = static_cast<int>(i);
      break;
    }
  }
  // The gaps before and after the tab itself do not move it.
  if (gap == current || gap == current + 1) {
    gap = wxNOT_FOUND;
  }
  if (gap != m_dropIndex) {
    m_dropIndex = gap;
    Refresh();
  }
}

void GlyphTabBar::DrawDropLine(wxDC &dc) const {
  const int index = m_dropIndex;
  if (m_tabs.empty() || index < 0 || index > static_cast<int>(m_tabs.size())) {
    return;
  }
  const bool vertical = IsVertical();
  const wxSize client = GetClientSize();
  // The line is on the edge between two tabs: the start of the tab after the
  // gap, or the end of the last tab.
  int edge = 0;
  if (index < static_cast<int>(m_tabs.size())) {
    const wxRect &rect = m_tabs[static_cast<size_t>(index)].info.rect;
    edge = vertical ? rect.y : rect.x;
  } else {
    const wxRect &rect = m_tabs.back().info.rect;
    edge = vertical ? rect.GetBottom() + 1 : rect.GetRight() + 1;
  }
  const int thickness = std::max(2, FromDIP(2));
  const int extent = vertical ? client.y : client.x;
  edge = std::clamp(edge - thickness / 2, 0, std::max(0, extent - thickness));

  // The same colour as the guide of the resize of the bar.
  dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
  dc.SetPen(*wxTRANSPARENT_PEN);
  const int inset = FromDIP(3);
  if (vertical) {
    dc.DrawRectangle(inset, edge, std::max(1, client.x - 2 * inset), thickness);
  } else {
    dc.DrawRectangle(edge, inset, thickness, std::max(1, client.y - 2 * inset));
  }
}

void GlyphTabBar::ShowTabList() {
  wxMenu menu;
  for (size_t i = 0; i < m_tabs.size(); ++i) {
    wxString label = m_tabs[i].info.caption;
    label.Replace(wxS("&"), wxS("&&"));
    wxMenuItem *item = menu.AppendCheckItem(wxID_HIGHEST + 1 + int(i), label);
    item->Check(static_cast<int>(i) == m_selection);
  }
  const int id = GetPopupMenuSelectionFromUser(
      menu, wxPoint(m_listRect.x, m_listRect.GetBottom() + 1));
  if (id >= wxID_HIGHEST + 1 && m_callbacks.select) {
    const size_t index = static_cast<size_t>(id - wxID_HIGHEST - 1);
    if (index < m_tabs.size()) {
      m_callbacks.select(index);
    }
  }
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
  m_dragStarted = false;
  m_dropIndex = -1;
  m_closePressed = nullptr;
  Refresh();
  if (resized && m_onWidthChanged) {
    m_onWidthChanged(m_width);
  }
}

void GlyphTabBar::OnCaptureLost(wxMouseCaptureLostEvent &WXUNUSED(event)) {
  EndMouseAction();
  // The mouse button was not released on the bar, so the hover state may be
  // old.
  UpdateHover(ScreenToClient(::wxGetMousePosition()));
  Refresh();
}
