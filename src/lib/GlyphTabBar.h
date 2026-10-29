#pragma once

// A vertical tab bar for a wxAuiNotebook. It needs GlyphTabArt and the
// wxWidgets 3.3 notebook API.
#include <wx/version.h>

#if wxCHECK_VERSION(3, 3, 0)

#include "GlyphTabArt.h"
#include <functional>
#include <memory>
#include <vector>
#include <wx/aui/auibook.h>
#include <wx/weakref.h>
#include <wx/window.h>

/**
 * @brief Vertical tabs for a wxAuiNotebook, painted with a GlyphTabArt.
 *
 * wxAUI has no vertical tabs: wxAUI_NB_LEFT and wxAUI_NB_RIGHT are not
 * implemented in wxWidgets 3.3. This bar is a sibling of the notebook, put on
 * its left or right side by the parent (see SetSide()). It shows the pages in
 * the order of the main tab control of the notebook. While the bar is used,
 * the notebook should not be split, and should hide its own tabs with
 * SetTabCtrlHeight(0).
 *
 * Like the tabs of the notebook, the bar:
 * - selects a page on a click;
 * - closes a page with the close button or a middle click, with the events
 *   wxEVT_AUINOTEBOOK_PAGE_CLOSE (which can veto it) and
 *   wxEVT_AUINOTEBOOK_PAGE_CLOSED of the notebook;
 * - sends wxEVT_AUINOTEBOOK_TAB_RIGHT_UP from the notebook on a right click;
 * - moves a page when its tab is dragged up or down.
 *
 * The edge of the bar next to the notebook can be dragged to change the width
 * of the bar. Too many tabs scroll with the mouse wheel.
 *
 * The bar follows the notebook by itself: it reads the pages again when the
 * selection changes, when a page is closed, and in idle time.
 */
class GlyphTabBar : public wxWindow {
public:
  // @p width is the width of the bar in DIPs.
  GlyphTabBar(wxWindow *parent, wxAuiNotebook *notebook, int width);
  ~GlyphTabBar() override = default;

  void SetArt(std::unique_ptr<GlyphTabArt> art);

  // The side of the notebook the bar is on: wxLEFT or wxRIGHT. The parent
  // puts the bar there; this only changes the drawing.
  void SetSide(wxDirection side);
  wxDirection GetSide() const { return m_side; }

  // The width of the bar in DIPs.
  void SetBarWidth(int width);
  int GetBarWidth() const { return m_width; }
  // Called with the new width in DIPs after the user resized the bar.
  void SetWidthChangedCallback(std::function<void(int)> callback) {
    m_onWidthChanged = std::move(callback);
  }

  // Read the pages of the notebook again, and paint the bar if they changed.
  void SyncWithNotebook();

  // The index (in the notebook) of the page whose tab is at @p pt, in the
  // coordinates of the bar, or wxNOT_FOUND.
  int PageIndexAt(const wxPoint &pt) const;

  bool AcceptsFocus() const override { return false; }
  bool AcceptsFocusFromKeyboard() const override { return false; }

private:
  struct Tab {
    // The rectangle of the row, and the rectangles of the buttons after the
    // tab was painted.
    wxAuiNotebookPage page;
    // The index of the page in the notebook.
    size_t index{0};
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
  void OnIdle(wxIdleEvent &event);
  void OnDpiChanged(wxDPIChangedEvent &event);
  void OnNotebookChanged(wxAuiNotebookEvent &event);

  // The side of the bar next to the notebook.
  wxDirection GetInnerSide() const {
    return m_side == wxLEFT ? wxRIGHT : wxLEFT;
  }
  // Place the rows from the top, with the scroll offset.
  void LayoutTabs();
  // The tab at @p pt, or nullptr.
  Tab *TabAt(const wxPoint &pt);
  // True if @p pt is on the close button of @p tab (as it was painted).
  bool IsOnCloseButton(const Tab &tab, const wxPoint &pt) const;
  // True if @p pt is on the edge that resizes the bar.
  bool IsOnResizeEdge(const wxPoint &pt) const;
  // Update the tab under the mouse. Returns true if something changed.
  bool UpdateHover(const wxPoint &pt);
  // Move the dragged tab to the row under @p pt.
  void DragTo(const wxPoint &pt);
  void ClosePage(size_t index);
  void ScrollBy(int dy);
  void EndMouseAction();

  wxWeakRef<wxAuiNotebook> m_notebook;
  std::unique_ptr<GlyphTabArt> m_art;
  std::vector<Tab> m_tabs;
  wxDirection m_side{wxLEFT};
  int m_width;
  std::function<void(int)> m_onWidthChanged;
  // The height of all rows, and the pixels scrolled off the top.
  int m_contentHeight{0};
  int m_scroll{0};

  // The page of the tab under the mouse, of the close button under the mouse,
  // and of the close button pressed.
  wxWindow *m_hover{nullptr};
  wxWindow *m_closeHover{nullptr};
  wxWindow *m_closePressed{nullptr};
  // The page of the tab being dragged.
  wxWindow *m_dragged{nullptr};
  // Resizing: the mouse x on the screen and the width (in pixels) when it
  // started.
  bool m_resizing{false};
  int m_resizeStartX{0};
  int m_resizeStartWidth{0};
};

#endif // wxCHECK_VERSION(3, 3, 0)
