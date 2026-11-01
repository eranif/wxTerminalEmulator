#pragma once

// A tabbed book with tabs on any side.
#include "BookLayout.h"
#include "GlyphTabBar.h"
#include "GlyphTabPainter.h"
#include "terminal_theme.h"
#include <functional>
#include <wx/aui/auibook.h>
#include <wx/bookctrl.h>
#include <wx/simplebook.h>

/**
 * @brief The event of GlyphBook. It is a wxBookCtrlEvent, so a handler that
 *        takes a wxBookCtrlEvent& works with it.
 *
 * - GetSelection() is the index of the tab, or wxNOT_FOUND.
 * - GetGroup() is the id of the group, or GlyphBook::kNoGroup.
 * - GetPosition() is the position of the mouse in the coordinates of the book:
 *   use it to show a context menu.
 */
class GlyphBookEvent : public wxBookCtrlEvent {
public:
  explicit GlyphBookEvent(wxEventType type = wxEVT_NULL, int id = 0,
                          int tab = wxNOT_FOUND, int oldTab = wxNOT_FOUND)
      : wxBookCtrlEvent(type, id, tab, oldTab) {}
  GlyphBookEvent(const GlyphBookEvent &event) = default;

  wxEvent *Clone() const override { return new GlyphBookEvent(*this); }

  int GetGroup() const { return m_group; }
  void SetGroup(int group) { m_group = group; }
  const wxPoint &GetPosition() const { return m_position; }
  void SetPosition(const wxPoint &position) { m_position = position; }

private:
  int m_group{-1};
  wxPoint m_position;

  wxDECLARE_DYNAMIC_CLASS_NO_ASSIGN(GlyphBookEvent);
};

// Events of GlyphBook that the standard book events (wxEVT_BOOKCTRL_PAGE_*) do
// not have. All of them are GlyphBookEvent.
//
// Pages:
// - PAGE_CLOSE: the user asks to close a page (close button, or a middle click
//   with wxAUI_NB_MIDDLE_CLICK_CLOSE). Veto() keeps the page.
// - PAGE_CLOSED: the page was closed.
//   Like with wxAuiNotebook, these two are sent only for a close by the user.
//   DeletePage() and RemovePage() send neither of them.
//
// Clicks. The book does not show any menu: the application handles these
// events and builds its own.
// - TAB_CONTEXT_MENU: the right button was released on a tab.
// - TAB_MIDDLE_UP: the middle button was released on a tab. A handler that does
//   not Skip() stops the close of the middle click.
// - GROUP_CONTEXT_MENU, GROUP_MIDDLE_UP: the same on the header of a group
//   (GetGroup()). Nothing happens by default.
// - BAR_CONTEXT_MENU, BAR_MIDDLE_UP: the same on the bar where there is no tab
//   or header. Nothing happens by default.
//
// Groups:
// - GROUP_CHANGED: the user collapsed or expanded a group (a click on its
//   header), or moved a tab into a group or out of it with a drag. The
//   functions of GlyphBook do not send it.
wxDECLARE_EVENT(wxEVT_GLYPHBOOK_PAGE_CLOSE, GlyphBookEvent);
wxDECLARE_EVENT(wxEVT_GLYPHBOOK_PAGE_CLOSED, GlyphBookEvent);
wxDECLARE_EVENT(wxEVT_GLYPHBOOK_TAB_CONTEXT_MENU, GlyphBookEvent);
wxDECLARE_EVENT(wxEVT_GLYPHBOOK_TAB_MIDDLE_UP, GlyphBookEvent);
wxDECLARE_EVENT(wxEVT_GLYPHBOOK_GROUP_CONTEXT_MENU, GlyphBookEvent);
wxDECLARE_EVENT(wxEVT_GLYPHBOOK_GROUP_MIDDLE_UP, GlyphBookEvent);
wxDECLARE_EVENT(wxEVT_GLYPHBOOK_BAR_CONTEXT_MENU, GlyphBookEvent);
wxDECLARE_EVENT(wxEVT_GLYPHBOOK_BAR_MIDDLE_UP, GlyphBookEvent);
wxDECLARE_EVENT(wxEVT_GLYPHBOOK_GROUP_CHANGED, GlyphBookEvent);

/**
 * @brief A book control with tabs at the top, at the bottom, on the left or on
 *        the right, painted with the colours of a terminal theme.
 *
 * Inside, it is a GlyphTabBar (the tabs) and a wxSimplebook (the pages). It
 * does not use wxAuiNotebook. Use it like a book control:
 *
 * @code
 * auto *book = new GlyphBook(parent, wxID_ANY, wxDefaultPosition,
 *                            wxDefaultSize,
 *                            wxAUI_NB_DEFAULT_STYLE | wxAUI_NB_LEFT);
 * book->SetTheme(theme);
 * book->AddPage(new wxTextCtrl(book->GetPagesParent(), wxID_ANY), "One", true);
 * book->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED, &MyFrame::OnPage, this);
 * @endcode
 *
 * **Tab position and style.** The window style tells where the tabs are:
 * wxAUI_NB_TOP, wxAUI_NB_BOTTOM, wxAUI_NB_LEFT or wxAUI_NB_RIGHT.
 * SetTabPosition() changes it at any time. The close buttons
 * (wxAUI_NB_CLOSE_ON_ACTIVE_TAB, wxAUI_NB_CLOSE_ON_ALL_TABS) and
 * wxAUI_NB_MIDDLE_CLICK_CLOSE work on all sides. The other wxAUI_NB_* flags
 * are ignored: the pages cannot be split, and a tab cannot be dragged to
 * another book, only to another place of the same bar.
 *
 * **Events.** The book sends the standard wxEVT_BOOKCTRL_PAGE_CHANGING (it can
 * be vetoed) and wxEVT_BOOKCTRL_PAGE_CHANGED when the user or SetSelection()
 * changes the page; ChangeSelection() sends none. When the current page is
 * deleted, the next tab is selected and only PAGE_CHANGED is sent (the old tab
 * is gone, so there is nothing to veto). The events for closing a page
 * and for the clicks on the tabs are the wxEVT_GLYPHBOOK_* events above.
 *
 * **Pages.** Create a page with GetPagesParent() as its parent. A page with
 * another parent (for example the book) is moved to GetPagesParent() when it
 * is added, which is slower, and can be a problem for some windows (for example
 * an OpenGL canvas). RemovePage() gives the page back to the book.
 */
class GlyphBook : public wxBookCtrlBase {
public:
  // The group of a page that is in no group.
  static constexpr int kNoGroup = GlyphTabBar::kNoGroup;

  GlyphBook();
  GlyphBook(wxWindow *parent, wxWindowID id = wxID_ANY,
            const wxPoint &pos = wxDefaultPosition,
            const wxSize &size = wxDefaultSize,
            long style = wxAUI_NB_DEFAULT_STYLE,
            const wxString &name = wxS("GlyphBook"));
  ~GlyphBook() override = default;

  bool Create(wxWindow *parent, wxWindowID id = wxID_ANY,
              const wxPoint &pos = wxDefaultPosition,
              const wxSize &size = wxDefaultSize,
              long style = wxAUI_NB_DEFAULT_STYLE,
              const wxString &name = wxS("GlyphBook"));

  // Appearance
  // ----------

  // Paint the tabs with the colours of @p theme. @p outline is the outline of
  // the current tab (the default is the magenta of the theme). @p barColour is
  // the colour behind the tabs. By default it is the system button face
  // (wxSYS_COLOUR_BTNFACE) in both styles. The current tab and the hovered tab
  // use the theme colours in both styles. Call it again after a theme change.
  // Until the first call the tabs use the dark theme.
  void SetTheme(const wxTerminalTheme &theme,
                const wxColour &outline = wxNullColour,
                GlyphTabPainter::Style style = GlyphTabPainter::Style::Pills,
                const wxColour &barColour = wxNullColour);
  // Paint the tabs with @p painter. The book takes it.
  void SetPainter(GlyphTabPainter *painter);

  // The side of the tabs: wxTOP, wxBOTTOM, wxLEFT or wxRIGHT. This is the same
  // as the wxAUI_NB_TOP, wxAUI_NB_BOTTOM, wxAUI_NB_LEFT and wxAUI_NB_RIGHT
  // flags of the window style.
  void SetTabPosition(wxDirection position);
  wxDirection GetTabPosition() const;
  bool HasVerticalTabs() const;

  void SetWindowStyleFlag(long style) override;

  // The width of the tab bar when the tabs are on the left or right, in DIPs.
  // The user can change it by dragging the edge of the bar.
  void SetTabBarWidth(int width);
  int GetTabBarWidth() const;
  // Called with the new width (in DIPs) after the user dragged the edge.
  void SetTabBarWidthChangedCallback(std::function<void(int)> callback);

  // The parent for the pages (see above).
  wxWindow *GetPagesParent() const { return m_pages; }
  // The bar of the tabs.
  GlyphTabBar *GetTabBar() const { return m_tabBar; }

  // Groups
  // ------
  //
  // A group is a header with a name and a colour, and the tabs after it. The
  // tabs of a group are always next to each other. A collapsed group shows only
  // its header. A group with the selected page is never collapsed: it is opened
  // when it gets the selection. The book shows no menu for the groups: the
  // application handles the context menu events and calls these functions.
  // The user can collapse a group with a click on its header, and can drag a
  // tab into a group or out of it (wxEVT_GLYPHBOOK_GROUP_CHANGED).

  // Make a group with the page in it. Returns the id of the group.
  int AddPageToNewGroup(size_t page, const wxString &name,
                        const wxColour &colour);
  // Move the page to the end of the group.
  bool AddPageToGroup(size_t page, int group);
  // A page in the middle of its group goes to the end of the group.
  void RemovePageFromGroup(size_t page);
  // Remove the group. The pages stay where they are.
  void Ungroup(int group);

  // The id of the group of the page, or kNoGroup.
  int GetPageGroup(size_t page) const;
  // The ids of the groups, in the order of their tabs.
  std::vector<int> GetGroups() const;
  std::vector<size_t> GetGroupPages(int group) const;
  wxString GetGroupName(int group) const;
  void SetGroupName(int group, const wxString &name);
  wxColour GetGroupColour(int group) const;
  void SetGroupColour(int group, const wxColour &colour);
  bool IsGroupCollapsed(int group) const;
  // Returns true if the state changed. A group with the selected page cannot be
  // collapsed.
  bool SetGroupCollapsed(int group, bool collapsed);
  // True if the selected page is in the group.
  bool IsSelectedInGroup(int group) const;

  // Colours for a menu of group colours.
  struct GroupColour {
    const char *name;
    wxColour colour;
  };
  static const std::vector<GroupColour> &GetGroupColours();

  // Pages
  // -----

  // The pages are in the order of their tabs.
  bool AddPage(wxWindow *page, const wxString &caption, bool select = false,
               const wxBitmapBundle &bitmap = wxBitmapBundle());
  bool InsertPage(size_t pageIdx, wxWindow *page, const wxString &caption,
                  bool select = false,
                  const wxBitmapBundle &bitmap = wxBitmapBundle());
  // The same with an image of the image list of the book.
  bool AddPage(wxWindow *page, const wxString &text, bool select,
               int imageId) override;
  bool InsertPage(size_t index, wxWindow *page, const wxString &text,
                  bool select, int imageId) override;

  bool DeletePage(size_t page) override;
  bool DeleteAllPages() override;

  size_t GetPageCount() const override;
  wxWindow *GetPage(size_t pageIdx) const override;
  int FindPage(const wxWindow *page) const override;
  // The same as FindPage().
  int GetPageIndex(wxWindow *page) const { return FindPage(page); }

  bool SetPageText(size_t page, const wxString &text) override;
  wxString GetPageText(size_t pageIdx) const override;
  bool SetPageToolTip(size_t page, const wxString &text);
  wxString GetPageToolTip(size_t pageIdx) const;
  bool SetPageBitmap(size_t page, const wxBitmapBundle &bitmap);
  wxBitmap GetPageBitmap(size_t pageIdx) const;
  // The image of the image list of the book. The image of a page is not kept:
  // the getter returns wxNOT_FOUND.
  bool SetPageImage(size_t page, int imageId) override;
  int GetPageImage(size_t page) const override;
  // The size of the pages is the size of the book: this does nothing.
  void SetPageSize(const wxSize &size) override;

  int GetSelection() const override;
  // Both return the old selection. SetSelection() sends the events.
  int SetSelection(size_t page) override;
  int ChangeSelection(size_t page) override;

  // The index of the page whose tab is at @p pt (in the coordinates of the
  // book), or wxNOT_FOUND.
  int HitTest(const wxPoint &pt, long *flags = nullptr) const override;

  // The layout of the tabs, in their order (see BookLayout).
  BookLayout SaveLayout() const;
  // Apply @p layout to the tabs that are in the book. The i-th entry is for
  // the i-th tab, so add the pages in the saved order first. Extra entries or
  // extra tabs are ignored. The selected entry becomes the current tab (with
  // the page events).
  void RestoreLayout(const BookLayout &layout);

protected:
  wxWindow *DoRemovePage(size_t page) override;
  wxSize DoGetBestSize() const override;

private:
  void Init();
  // Put the bar on its side, and set what it shows.
  void UpdateLayout();
  void OnSize(wxSizeEvent &event);
  // Make the painter again from the stored theme (see SetTheme()).
  void RebuildPainter();
  // Make the painter again when the system colours change (the default bar
  // colour is the button face).
  void OnSysColourChanged(wxSysColourChangedEvent &event);
  // Give the painter to the bar.
  void ApplyPainter(std::unique_ptr<GlyphTabPainter> painter);

  // Send an event with @p selection (and the old one), and the group and the
  // position of the mouse in the coordinates of the bar, if any. Returns false
  // if a handler vetoed it. @p handled tells if a handler processed it.
  bool SendEvent(wxEventType type, int selection,
                 int oldSelection = wxNOT_FOUND, bool *handled = nullptr,
                 int group = kNoGroup,
                 const wxPoint &position = wxDefaultPosition);
  int DoSetSelection(size_t page, bool sendEvents);
  // Show the page window of the tab @p index in the pages.
  void ShowPage(int index);
  // Remove the tab and the page. A page that is not destroyed is not a child of
  // the pages any more. Selects the tab next to the removed one.
  wxWindow *RemoveTab(size_t index, bool destroy);
  // Ask the application, then close the page.
  void ClosePage(size_t index);

  wxSimplebook *m_pages{nullptr};
  GlyphTabBar *m_tabBar{nullptr};
  // The window style, with the flags that the base class does not know.
  long m_style{wxAUI_NB_DEFAULT_STYLE};
  int m_tabBarWidth{200};
  // The last SetTheme() call. The painter is made again from it when the system
  // colours change. Not used after SetPainter().
  wxTerminalTheme m_theme;
  wxColour m_outline;
  GlyphTabPainter::Style m_tabStyle{GlyphTabPainter::Style::Pills};
  wxColour m_barColour;
  bool m_themed{false};
};
