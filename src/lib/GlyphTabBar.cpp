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
  // A tab between two tabs of a group is a part of the group: the tabs of a
  // group stay together.
  if (index > 0 && index < m_tabs.size() &&
      m_tabs[index - 1].group != kNoGroup &&
      m_tabs[index - 1].group == m_tabs[index].group) {
    tab.group = m_tabs[index].group;
  }
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
  RemoveEmptyGroups();
  UpdateButtons();
  UpdateMinSize();
  LayoutTabs();
  Refresh();
}

void GlyphTabBar::Clear() {
  m_tabs.clear();
  m_groups.clear();
  m_items.clear();
  m_dropIndex = -1;
  m_selection = -1;
  m_hover = m_closeHover = m_closePressed = m_dragged = nullptr;
  m_hoverGroup = m_groupPressed = kNoGroup;
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

// Groups
// ------

GlyphTabBar::Group *GlyphTabBar::FindGroup(int group) {
  for (auto &candidate : m_groups) {
    if (candidate.id == group) {
      return &candidate;
    }
  }
  return nullptr;
}

const GlyphTabBar::Group *GlyphTabBar::FindGroup(int group) const {
  for (const auto &candidate : m_groups) {
    if (candidate.id == group) {
      return &candidate;
    }
  }
  return nullptr;
}

std::vector<int> GlyphTabBar::GetGroups() const {
  std::vector<int> groups;
  for (const auto &tab : m_tabs) {
    if (tab.group != kNoGroup &&
        std::find(groups.begin(), groups.end(), tab.group) == groups.end()) {
      groups.push_back(tab.group);
    }
  }
  return groups;
}

std::vector<size_t> GlyphTabBar::GetGroupTabs(int group) const {
  std::vector<size_t> tabs;
  for (size_t i = 0; i < m_tabs.size(); ++i) {
    if (m_tabs[i].group == group) {
      tabs.push_back(i);
    }
  }
  return tabs;
}

const wxString &GlyphTabBar::GetGroupName(int group) const {
  static const wxString none;
  const Group *found = FindGroup(group);
  return found ? found->info.name : none;
}

void GlyphTabBar::SetGroupName(int group, const wxString &name) {
  if (Group *found = FindGroup(group); found && found->info.name != name) {
    found->info.name = name;
    LayoutTabs();
    Refresh();
  }
}

const wxColour &GlyphTabBar::GetGroupColour(int group) const {
  static const wxColour none;
  const Group *found = FindGroup(group);
  return found ? found->info.colour : none;
}

void GlyphTabBar::SetGroupColour(int group, const wxColour &colour) {
  if (Group *found = FindGroup(group)) {
    found->info.colour =
        colour.IsOk() ? colour : GlyphTabPainter::DefaultGroupColour();
    Refresh();
  }
}

bool GlyphTabBar::IsGroupCollapsed(int group) const {
  const Group *found = FindGroup(group);
  return found && found->info.collapsed;
}

void GlyphTabBar::SetGroupCollapsed(int group, bool collapsed) {
  if (Group *found = FindGroup(group);
      found && found->info.collapsed != collapsed) {
    found->info.collapsed = collapsed;
    LayoutTabs();
    Refresh();
  }
}

int GlyphTabBar::AddTabToNewGroup(size_t index, const wxString &name,
                                  const wxColour &colour) {
  if (index >= m_tabs.size()) {
    return kNoGroup;
  }
  // Out of the old group first: a tab in the middle of it would cut it in two.
  index = DetachTab(index);
  Group group;
  group.id = m_nextGroupId++;
  group.info.name = name;
  group.info.colour =
      colour.IsOk() ? colour : GlyphTabPainter::DefaultGroupColour();
  m_groups.push_back(group);
  AddTabToGroup(index, group.id);
  return group.id;
}

bool GlyphTabBar::AddTabToGroup(size_t index, int group) {
  if (index >= m_tabs.size() || !FindGroup(group)) {
    return false;
  }
  if (m_tabs[index].group == group) {
    return true;
  }
  // The end of the group: the tab goes after its last tab.
  int last = -1;
  for (size_t i = 0; i < m_tabs.size(); ++i) {
    if (m_tabs[i].group == group) {
      last = static_cast<int>(i);
    }
  }
  m_tabs[index].group = group;
  if (last != -1) {
    // The index of the tab after the move: the tabs after it move one place
    // down when it is taken out from before them.
    Move(index,
         static_cast<size_t>(static_cast<int>(index) < last ? last : last + 1));
  }
  RemoveEmptyGroups();
  LayoutTabs();
  Refresh();
  return true;
}

size_t GlyphTabBar::DetachTab(size_t index) {
  if (index >= m_tabs.size() || m_tabs[index].group == kNoGroup) {
    return index;
  }
  const int group = m_tabs[index].group;
  const bool sameBefore = index > 0 && m_tabs[index - 1].group == group;
  const bool sameAfter =
      index + 1 < m_tabs.size() && m_tabs[index + 1].group == group;
  m_tabs[index].group = kNoGroup;
  if (!sameBefore || !sameAfter) {
    return index;
  }
  // In the middle of the group: it would cut the group in two, so the tab goes
  // to the end of the group.
  size_t last = index;
  while (last + 1 < m_tabs.size() && m_tabs[last + 1].group == group) {
    ++last;
  }
  Move(index, last);
  return last;
}

void GlyphTabBar::RemoveTabFromGroup(size_t index) {
  DetachTab(index);
  RemoveEmptyGroups();
  LayoutTabs();
  Refresh();
}

void GlyphTabBar::Ungroup(int group) {
  for (auto &tab : m_tabs) {
    if (tab.group == group) {
      tab.group = kNoGroup;
    }
  }
  RemoveEmptyGroups();
  LayoutTabs();
  Refresh();
}

void GlyphTabBar::RemoveEmptyGroups() {
  const auto hasTabs = [this](const Group &group) {
    return std::any_of(m_tabs.begin(), m_tabs.end(), [&group](const Tab &tab) {
      return tab.group == group.id;
    });
  };
  for (size_t i = m_groups.size(); i-- > 0;) {
    if (!hasTabs(m_groups[i])) {
      if (m_hoverGroup == m_groups[i].id) {
        m_hoverGroup = kNoGroup;
      }
      if (m_groupPressed == m_groups[i].id) {
        m_groupPressed = kNoGroup;
      }
      m_groups.erase(m_groups.begin() + i);
    }
  }
}

bool GlyphTabBar::IsTabVisible(size_t index) const {
  const Tab &tab = m_tabs[index];
  if (tab.group == kNoGroup) {
    return true;
  }
  // The selected tab of a collapsed group stays visible.
  const Group *group = FindGroup(tab.group);
  return !group || !group->info.collapsed ||
         static_cast<int>(index) == m_selection;
}

int GlyphTabBar::GroupAt(const wxPoint &pt) const {
  if (!m_listRect.IsEmpty() && pt.x >= m_tabsRoom) {
    return kNoGroup;
  }
  for (const auto &group : m_groups) {
    if (group.info.rect.Contains(pt)) {
      return group.id;
    }
  }
  return kNoGroup;
}

int GlyphTabBar::GroupForDrop(size_t from, size_t gap) const {
  if (from >= m_tabs.size()) {
    return kNoGroup;
  }
  const size_t to = gap > from ? gap - 1 : gap;
  // The tabs without the one that is dropped: the tab goes between the one
  // before `to` and the one at `to`.
  const size_t count = m_tabs.size() - 1;
  const auto groupOf = [this, from](size_t k) {
    return m_tabs[k < from ? k : k + 1].group;
  };
  const int before = to > 0 ? groupOf(to - 1) : kNoGroup;
  const int after = to < count ? groupOf(to) : kNoGroup;
  return before == after ? before : kNoGroup;
}

void GlyphTabBar::DropTab(size_t from, size_t gap) {
  if (from >= m_tabs.size()) {
    return;
  }
  const size_t to = gap > from ? gap - 1 : gap;
  const int oldGroup = m_tabs[from].group;
  Move(from, to);
  // The tab joins the group that is on both of its sides, and is in no group
  // anywhere else: dropped in the middle of a group it is a part of the group,
  // and dropped at the end of a group, or out of it, it is not.
  const int before = to > 0 ? m_tabs[to - 1].group : kNoGroup;
  const int after = to + 1 < m_tabs.size() ? m_tabs[to + 1].group : kNoGroup;
  const int newGroup = before == after ? before : kNoGroup;
  if (newGroup == oldGroup) {
    return;
  }
  m_tabs[to].group = newGroup;
  RemoveEmptyGroups();
  LayoutTabs();
  Refresh();
  if (m_callbacks.groupChanged) {
    m_callbacks.groupChanged(newGroup != kNoGroup ? newGroup : oldGroup);
  }
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
  const bool vertical = IsVertical();
  m_listRect = wxRect();
  m_items.clear();
  for (auto &group : m_groups) {
    group.info.rect = wxRect();
    group.spanStart = group.spanEnd = 0;
  }
  // A tab that is not placed below is hidden: it has an empty rectangle.
  for (auto &tab : m_tabs) {
    tab.info.rect = wxRect();
  }

  // The items in order, and their sizes along the bar: the width in a
  // horizontal bar, the height in a vertical one.
  std::vector<int> sizes;
  // The items that are tabs. In a horizontal bar they can get narrower.
  std::vector<size_t> flexible;
  int previousGroup = kNoGroup;
  for (size_t i = 0; i < m_tabs.size(); ++i) {
    const Tab &tab = m_tabs[i];
    if (tab.group != previousGroup) {
      previousGroup = tab.group;
      if (const Group *group = FindGroup(tab.group)) {
        Item header;
        header.header = true;
        header.index = group->id;
        header.firstTab = static_cast<int>(i);
        m_items.push_back(header);
        sizes.push_back(
            vertical
                ? m_painter->GetVerticalGroupHeaderHeight(this, group->info)
                : m_painter->GetGroupHeaderSize(this, group->info).x);
      }
    }
    if (IsTabVisible(i)) {
      Item item;
      item.index = static_cast<int>(i);
      item.firstTab = static_cast<int>(i);
      m_items.push_back(item);
      flexible.push_back(m_items.size() - 1);
      sizes.push_back(vertical ? m_painter->GetVerticalTabHeight(this, tab.info)
                               : m_painter->GetTabSize(this, tab.info).x);
    }
  }
  int total = 0;
  for (const int size : sizes) {
    total += size;
  }

  m_tabsRoom = vertical ? client.y : client.x;
  if (!vertical) {
    // The tabs get narrower when they do not fit; when they are as narrow as
    // they can be and still do not fit, the bar scrolls and has the list
    // button.
    if (total > client.x && !flexible.empty()) {
      std::vector<int> widths;
      int fixedTotal = total;
      for (const size_t item : flexible) {
        widths.push_back(sizes[item]);
        fixedTotal -= sizes[item];
      }
      ShrinkWidths(widths, std::max(0, client.x - fixedTotal),
                   FromDIP(kMinTabWidth));
      total = fixedTotal;
      for (size_t k = 0; k < flexible.size(); ++k) {
        sizes[flexible[k]] = widths[k];
        total += widths[k];
      }
    }
    if (total > client.x) {
      const int listWidth = FromDIP(kListWidth);
      m_tabsRoom = std::max(0, client.x - listWidth);
      m_listRect = wxRect(m_tabsRoom, 0, client.x - m_tabsRoom, client.y);
    }
  }
  m_contentSize = total;
  m_scroll = std::clamp(m_scroll, 0, std::max(0, total - m_tabsRoom));

  int position = -m_scroll;
  for (size_t k = 0; k < m_items.size(); ++k) {
    Item &item = m_items[k];
    item.rect = vertical ? wxRect(0, position, client.x, sizes[k])
                         : wxRect(position, 0, sizes[k], client.y);
    if (item.header) {
      Group *group = FindGroup(item.index);
      group->info.rect = item.rect;
      group->spanStart = position;
      group->spanEnd = position + sizes[k];
    } else {
      Tab &tab = m_tabs[static_cast<size_t>(item.index)];
      tab.info.rect = item.rect;
      if (Group *group = FindGroup(tab.group)) {
        group->spanEnd = position + sizes[k];
      }
    }
    position += sizes[k];
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
    // The tabs that are not in the window, and the hidden ones (an empty
    // rectangle), are not painted, so their buttons cannot be clicked.
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

  for (auto &group : m_groups) {
    group.info.hover = group.id == m_hoverGroup;
    const wxRect rect = group.info.rect;
    if (rect.Intersects(client)) {
      m_painter->DrawGroupHeader(dc, this, group.info, rect, inner);
    }
  }

  // The line of each group is along its header and its tabs. It is drawn after
  // them, or the hover and the current tab would cover it.
  for (const auto &group : m_groups) {
    if (group.info.rect.IsEmpty()) {
      continue;
    }
    const wxRect span =
        IsVertical() ? wxRect(0, group.spanStart, client.width,
                              group.spanEnd - group.spanStart)
                     : wxRect(group.spanStart, 0,
                              group.spanEnd - group.spanStart, client.height);
    if (span.Intersects(client)) {
      m_painter->DrawGroupLine(dc, this, span, group.info.colour, inner);
    }
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
  const int hoverGroup = inside ? GroupAt(pt) : kNoGroup;
  const bool listHover = inside && m_listRect.Contains(pt);
  if (hover == m_hover && closeHover == m_closeHover &&
      hoverGroup == m_hoverGroup && listHover == m_listHover) {
    return false;
  }
  if (hover != m_hover || hoverGroup != m_hoverGroup) {
    // The tooltip of the page, or the whole label, for a label that does not
    // fit; the name of a group.
    if (index != wxNOT_FOUND) {
      const GlyphTabInfo &info = m_tabs[index].info;
      SetToolTip(info.tooltip.empty() ? info.caption : info.tooltip);
    } else if (const Group *group = FindGroup(hoverGroup);
               group && !group->info.name.empty()) {
      SetToolTip(group->info.name);
    } else {
      UnsetToolTip();
    }
  }
  m_hover = hover;
  m_closeHover = closeHover;
  m_hoverGroup = hoverGroup;
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

  if (const int group = GroupAt(pt); group != kNoGroup) {
    // A click on a header collapses or expands the group, on release.
    m_groupPressed = group;
    CaptureMouse();
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
  const int groupPressed = m_groupPressed;
  EndMouseAction();
  if (dragged && drop != wxNOT_FOUND) {
    // The gap is counted before the tab is taken out of the list.
    const int from = Find(dragged);
    if (from != wxNOT_FOUND) {
      DropTab(static_cast<size_t>(from), static_cast<size_t>(drop));
    }
  }
  if (groupPressed != kNoGroup &&
      GroupAt(event.GetPosition()) == groupPressed) {
    SetGroupCollapsed(groupPressed, !IsGroupCollapsed(groupPressed));
    if (m_callbacks.groupChanged) {
      m_callbacks.groupChanged(groupPressed);
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
  const wxPoint pt = event.GetPosition();
  if (const int index = TabIndexAt(pt); index != wxNOT_FOUND) {
    if (m_callbacks.tabMiddleUp) {
      m_callbacks.tabMiddleUp(static_cast<size_t>(index), pt);
    }
  } else if (const int group = GroupAt(pt); group != kNoGroup) {
    if (m_callbacks.groupMiddleUp) {
      m_callbacks.groupMiddleUp(group, pt);
    }
  } else if (!m_listRect.Contains(pt) && m_callbacks.barMiddleUp) {
    m_callbacks.barMiddleUp(pt);
  }
}

void GlyphTabBar::OnRightUp(wxMouseEvent &event) {
  const wxPoint pt = event.GetPosition();
  if (const int index = TabIndexAt(pt); index != wxNOT_FOUND) {
    if (m_callbacks.tabContextMenu) {
      m_callbacks.tabContextMenu(static_cast<size_t>(index), pt);
    }
  } else if (const int group = GroupAt(pt); group != kNoGroup) {
    if (m_callbacks.groupContextMenu) {
      m_callbacks.groupContextMenu(group, pt);
    }
  } else if (!m_listRect.Contains(pt) && m_callbacks.barContextMenu) {
    m_callbacks.barContextMenu(pt);
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
    step = FromDIP(24);
    for (const Item &item : m_items) {
      if (!item.header) {
        step = item.rect.height;
        break;
      }
    }
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
  // The gap under the mouse: before the first item (a tab or the header of a
  // group) whose middle is after the mouse, or after the last tab. The gap is
  // the number of the tab it is before.
  const int position = IsVertical() ? pt.y : pt.x;
  int gap = static_cast<int>(m_tabs.size());
  for (const Item &item : m_items) {
    const wxRect &rect = item.rect;
    const int middle =
        IsVertical() ? rect.y + rect.height / 2 : rect.x + rect.width / 2;
    if (position < middle) {
      gap = item.firstTab;
      break;
    }
  }
  // The gaps before and after the tab itself do not move it. They take it out
  // of its group only when the mouse is really out of the group, on the item
  // next to it: the group of the tab is then different. Over the header, or
  // over the first or the last tab of the group (even over the half of it that
  // is next to the edge), a small move of the mouse changes nothing.
  if (gap == current || gap == current + 1) {
    const Tab &dragged = m_tabs[static_cast<size_t>(current)];
    const Group *group = FindGroup(dragged.group);
    const bool inGroup =
        group && position >= group->spanStart && position < group->spanEnd;
    if (inGroup || GroupForDrop(static_cast<size_t>(current),
                                static_cast<size_t>(gap)) == dragged.group) {
      gap = wxNOT_FOUND;
    }
  }
  if (gap != m_dropIndex) {
    m_dropIndex = gap;
    Refresh();
  }
}

void GlyphTabBar::DrawDropLine(wxDC &dc) const {
  const int index = m_dropIndex;
  if (m_items.empty() || index < 0) {
    return;
  }
  const bool vertical = IsVertical();
  const wxSize client = GetClientSize();
  // The line is on the edge between two items: the start of the first item at
  // or after the gap (a header comes before the first tab of its group), or the
  // end of the last item.
  int edge = 0;
  const auto item = std::find_if(
      m_items.begin(), m_items.end(),
      [index](const Item &candidate) { return candidate.firstTab >= index; });
  if (item != m_items.end()) {
    edge = vertical ? item->rect.y : item->rect.x;
  } else {
    const wxRect &rect = m_items.back().rect;
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
  m_groupPressed = kNoGroup;
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
