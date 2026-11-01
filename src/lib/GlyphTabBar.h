#pragma once

// A tab bar, painted with GlyphTabPainter.
#include "GlyphTabPainter.h"
#include <functional>
#include <memory>
#include <vector>
#include <wx/popupwin.h>
#include <wx/window.h>

/**
 * @brief A bar of tabs on any side of the pages (see GlyphBook).
 *
 * The bar keeps the tabs: for each one a page window (it is not owned and not
 * shown by the bar), a caption, an icon and a tooltip. It knows nothing about
 * the pages or the notebook events. It tells its owner what the user did
 * through the callbacks (see SetCallbacks()), and the owner changes the bar.
 *
 * - Tabs on the top or the bottom are placed from left to right. When they do
 *   not fit, they get narrower, and then the bar scrolls (mouse wheel) and
 * shows a button with the list of all the tabs.
 * - Tabs on the left or the right are rows, from top to bottom. The bar scrolls
 *   with the mouse wheel, and its inner edge can be dragged to change its
 * width.
 * - A tab is selected by a click. Dragging it shows a line where it will go,
 *   and it moves when the mouse is released.
 */
class GlyphTabBar : public wxWindow {
public:
  struct Callbacks {
    // The user clicked a tab (or chose it in the list). Called with the index.
    std::function<void(size_t)> select;
    // The user clicked the close button of a tab.
    std::function<void(size_t)> close;
    // The mouse buttons were released on a tab, on the header of a group, or on
    // the bar where there is neither. The position is in the coordinates of
    // the bar.
    std::function<void(size_t, const wxPoint &)> tabContextMenu;
    std::function<void(size_t, const wxPoint &)> tabMiddleUp;
    std::function<void(int, const wxPoint &)> groupContextMenu;
    std::function<void(int, const wxPoint &)> groupMiddleUp;
    std::function<void(const wxPoint &)> barContextMenu;
    std::function<void(const wxPoint &)> barMiddleUp;
    // The user collapsed or expanded a group (a click on its header), or moved
    // a tab into a group or out of it. Called with the group.
    std::function<void(int)> groupChanged;
  };

  // The group of a tab that is in no group.
  static constexpr int kNoGroup = -1;

  // @p width is the width of the bar in DIPs, when it is on the left or the
  // right.
  GlyphTabBar(wxWindow *parent, int width);
  ~GlyphTabBar() override = default;

  void SetPainter(std::unique_ptr<GlyphTabPainter> painter);
  void SetCallbacks(Callbacks callbacks) { m_callbacks = std::move(callbacks); }

  // The side of the pages the bar is on: wxTOP, wxBOTTOM, wxLEFT or wxRIGHT.
  // The parent puts the bar there; this changes the drawing and the layout of
  // the tabs.
  void SetSide(wxDirection side);
  wxDirection GetSide() const { return m_side; }
  bool IsVertical() const { return m_side == wxLEFT || m_side == wxRIGHT; }

  // The width of the bar in DIPs when it is on the left or the right.
  void SetBarWidth(int width);
  int GetBarWidth() const { return m_width; }
  // Called with the new width in DIPs after the user resized the bar.
  void SetWidthChangedCallback(std::function<void(int)> callback) {
    m_onWidthChanged = std::move(callback);
  }

  // Which tabs have a close button: all, or only the selected one.
  void SetCloseButtons(bool onAllTabs, bool onActiveTab);

  // Tabs
  // ----

  size_t GetCount() const { return m_tabs.size(); }
  // Add a tab before @p index (the end when it is the count). The selection
  // does not change, except that it follows the tab it had.
  void Insert(size_t index, wxWindow *window, const wxString &caption,
              const wxBitmapBundle &bitmap);
  // Remove a tab. The selection becomes -1 when it was this tab.
  void Remove(size_t index);
  void Clear();
  // Move the tab @p from to the place @p to (an index in the list after the
  // move).
  void Move(size_t from, size_t to);

  wxWindow *GetWindow(size_t index) const { return m_tabs[index].window; }
  int Find(const wxWindow *window) const;

  void SetCaption(size_t index, const wxString &caption);
  const wxString &GetCaption(size_t index) const {
    return m_tabs[index].info.caption;
  }
  void SetTooltip(size_t index, const wxString &tooltip);
  const wxString &GetTooltip(size_t index) const {
    return m_tabs[index].info.tooltip;
  }
  void SetBitmap(size_t index, const wxBitmapBundle &bitmap);
  const wxBitmapBundle &GetBitmap(size_t index) const {
    return m_tabs[index].info.bitmap;
  }

  // The selected tab, or -1. The bar scrolls to show it.
  void SetSelection(int index);
  int GetSelection() const { return m_selection; }

  // The index of the tab at @p pt (in the coordinates of the bar), or
  // wxNOT_FOUND.
  int HitTest(const wxPoint &pt) const;

  // Groups
  // ------
  //
  // A group is a header (a chip with a name and a colour) and the tabs after
  // it. The tabs of a group are always next to each other. A group without tabs
  // is removed. A collapsed group shows only its header. A group with the
  // selected tab is never collapsed: it is opened when it gets the selection.

  // Make a group with the tab @p index in it. Returns the id of the group.
  int AddTabToNewGroup(size_t index, const wxString &name,
                       const wxColour &colour);
  // Move the tab to the end of the group, and make it a part of it.
  bool AddTabToGroup(size_t index, int group);
  // Take the tab out of its group. If it is in the middle of the group, it goes
  // to the end of it.
  void RemoveTabFromGroup(size_t index);
  // Remove the group; its tabs stay where they are.
  void Ungroup(int group);

  bool HasGroup(int group) const { return FindGroup(group) != nullptr; }
  int GetTabGroup(size_t index) const { return m_tabs[index].group; }
  // The ids of the groups in the order of their tabs.
  std::vector<int> GetGroups() const;
  // The indexes of the tabs of the group.
  std::vector<size_t> GetGroupTabs(int group) const;
  const wxString &GetGroupName(int group) const;
  void SetGroupName(int group, const wxString &name);
  const wxColour &GetGroupColour(int group) const;
  void SetGroupColour(int group, const wxColour &colour);
  bool IsGroupCollapsed(int group) const;
  // Returns true if the state changed. A group with the selected tab cannot be
  // collapsed.
  bool SetGroupCollapsed(int group, bool collapsed);
  // True if the selected tab is in the group.
  bool IsSelectedInGroup(int group) const;

  bool AcceptsFocus() const override { return false; }
  bool AcceptsFocusFromKeyboard() const override { return false; }

private:
  struct Tab {
    GlyphTabInfo info;
    wxWindow *window{nullptr};
    int group{kNoGroup};
  };

  struct Group {
    int id{kNoGroup};
    GlyphGroupInfo info;
    // Where its header and its tabs are along the bar, after the last layout.
    int spanStart{0};
    int spanEnd{0};
  };
  // What the bar shows, in order: a header of a group or a tab that is not
  // hidden. Set by LayoutTabs().
  struct Item {
    bool header{false};
    // The index of the tab, or the id of the group.
    int index{0};
    // The index of the tab, or of the first tab of the group.
    int firstTab{0};
    wxRect rect;
  };

  void OnPaint(wxPaintEvent &event);
  void OnSize(wxSizeEvent &event);
  void OnMotion(wxMouseEvent &event);
  void OnLeave(wxMouseEvent &event);
  void OnLeftDown(wxMouseEvent &event);
  void OnLeftUp(wxMouseEvent &event);
  void OnMiddleUp(wxMouseEvent &event);
  void OnRightUp(wxMouseEvent &event);
  void OnWheel(wxMouseEvent &event);
  void OnCaptureLost(wxMouseCaptureLostEvent &event);
  void OnDpiChanged(wxDPIChangedEvent &event);

  // The side of the bar next to the pages.
  wxDirection GetInnerSide() const;
  // The close buttons of the tabs, for the selection and the options.
  void UpdateButtons();
  // Set the size the parent gives to the bar: the width, or the height of the
  // tabs.
  void UpdateMinSize();
  // Ask the parent to layout again, once, after the events that are waiting.
  void RequestParentLayout();
  // Place the tabs, with the scroll offset.
  void LayoutTabs();
  // A tab of a collapsed group is hidden.
  bool IsTabVisible(size_t index) const;
  Group *FindGroup(int group);
  const Group *FindGroup(int group) const;
  // The id of the group whose header is at @p pt, or kNoGroup.
  int GroupAt(const wxPoint &pt) const;
  // Remove the groups that have no tabs.
  void RemoveEmptyGroups();
  // Take the tab out of its group. A tab in the middle of its group goes to
  // the end of it, so the group stays in one piece. Returns the index of the
  // tab after that.
  size_t DetachTab(size_t index);
  // The group the tab @p from would be in if it were dropped in the gap @p gap
  // (see DropTab()).
  int GroupForDrop(size_t from, size_t gap) const;
  // Move the tab @p from to the gap @p gap (a number of tab, before which the
  // tab goes) and put it in the group of its new neighbours.
  void DropTab(size_t from, size_t gap);
  // Scroll so that the tab @p index is inside the bar.
  void EnsureVisible(size_t index);
  // The index of the tab at @p pt (a tab under the list button is not found).
  int TabIndexAt(const wxPoint &pt) const;
  // True if @p pt is on the close button of the tab (as it was painted).
  bool IsOnCloseButton(const Tab &tab, const wxPoint &pt) const;
  // True if @p pt is on the edge that resizes the bar.
  bool IsOnResizeEdge(const wxPoint &pt) const;
  // Update the tab under the mouse. Returns true if something changed.
  bool UpdateHover(const wxPoint &pt);
  // While a tab is dragged: show where it would go. The tab moves on release.
  void DragTo(const wxPoint &pt);
  // The line that shows where the dragged tab will go.
  void DrawDropLine(wxDC &dc) const;
  // Show the list of all the tabs, and call the select callback.
  void ShowTabList();
  void ScrollBy(int delta);
  void EndMouseAction();
  // @p width (in DIPs) inside the limits of the bar.
  int ClampWidth(int width) const;
  // Show the guide line at the edge the bar will have with m_dragWidth.
  void MoveGuide();

  std::unique_ptr<GlyphTabPainter> m_painter;
  Callbacks m_callbacks;
  std::vector<Tab> m_tabs;
  std::vector<Group> m_groups;
  int m_nextGroupId{1};
  std::vector<Item> m_items;
  int m_selection{-1};
  bool m_closeOnAll{false};
  bool m_closeOnActive{false};
  wxDirection m_side{wxTOP};
  int m_width;
  std::function<void(int)> m_onWidthChanged;
  // The size of all the tabs along the bar, and the pixels scrolled off the
  // start.
  int m_contentSize{0};
  int m_scroll{0};
  // The part of a wheel notch that did not scroll yet (touchpads and
  // high-resolution wheels send less than one notch at a time).
  int m_wheelRemainder{0};
  // The room of the tabs along the bar (less than the bar with the list
  // button), and the rectangle of the list button (empty without it).
  int m_tabsRoom{0};
  wxRect m_listRect;
  bool m_listHover{false};

  // The page of the tab under the mouse, of the close button under the mouse,
  // and of the close button pressed.
  wxWindow *m_hover{nullptr};
  wxWindow *m_closeHover{nullptr};
  wxWindow *m_closePressed{nullptr};
  // The group whose header is under the mouse, and the one pressed.
  int m_hoverGroup{kNoGroup};
  int m_groupPressed{kNoGroup};
  // The page of the tab being dragged, where the mouse went down, and if it
  // moved far enough to start the drag.
  wxWindow *m_dragged{nullptr};
  wxPoint m_pressPoint;
  bool m_dragStarted{false};
  // While a tab is dragged: where it would go if it were dropped now. It is
  // the number of the gap before the tab with this index (the count is the gap
  // after the last tab), or -1 when the drop would not move the tab.
  int m_dropIndex{-1};
  // Resizing: the mouse x on the screen and the width (in pixels) when it
  // started.
  bool m_resizing{false};
  // A layout of the parent is waiting (see RequestParentLayout()).
  bool m_layoutPending{false};
  int m_resizeStartX{0};
  int m_resizeStartWidth{0};
  // The width (in DIPs) the drag will set, and the line that shows it.
  int m_dragWidth{0};
  wxPopupWindow *m_guide{nullptr};
};
