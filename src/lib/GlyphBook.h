#pragma once

// A tabbed book with tabs on any side. It needs wxWidgets 3.3 (see
// GlyphTabArt); with an older wxWidgets use wxAuiNotebook.
#include <wx/version.h>

#if wxCHECK_VERSION(3, 3, 0)

#include "BookLayout.h"
#include "GlyphTabArt.h"
#include "GlyphTabBar.h"
#include "terminal_theme.h"
#include <functional>
#include <wx/bookctrl.h>
#include <wx/simplebook.h>

// Events of GlyphBook that the standard book events do not have. They are
// wxBookCtrlEvent, with GetSelection() the index of the tab.
//
// - PAGE_CLOSE: the user asks to close a page (close button, or a middle click
//   with wxAUI_NB_MIDDLE_CLICK_CLOSE). Veto() keeps the page.
// - PAGE_CLOSED: the page was closed.
//   Like with wxAuiNotebook, these two are sent only for a close by the user.
//   DeletePage() and RemovePage() send neither of them.
// - TAB_RIGHT_UP, TAB_MIDDLE_UP: a click on a tab. A handler of TAB_MIDDLE_UP
//   that does not Skip() stops the close of the middle click.
wxDECLARE_EVENT(wxEVT_GLYPHBOOK_PAGE_CLOSE, wxBookCtrlEvent);
wxDECLARE_EVENT(wxEVT_GLYPHBOOK_PAGE_CLOSED, wxBookCtrlEvent);
wxDECLARE_EVENT(wxEVT_GLYPHBOOK_TAB_RIGHT_UP, wxBookCtrlEvent);
wxDECLARE_EVENT(wxEVT_GLYPHBOOK_TAB_MIDDLE_UP, wxBookCtrlEvent);

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
                GlyphTabArt::Style style = GlyphTabArt::Style::Pills,
                const wxColour &barColour = wxNullColour);
  // Paint the tabs with @p art. The book takes it.
  void SetArtProvider(GlyphTabArt *art);

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
  // Make the art again from the stored theme (see SetTheme()).
  void RebuildArt();
  // Make the art again when the system colours change (the default bar colour
  // is the button face).
  void OnSysColourChanged(wxSysColourChangedEvent &event);
  // Give the art to the bar.
  void ApplyArt(std::unique_ptr<GlyphTabArt> art);

  // Send an event with @p selection (and the old one). Returns false if a
  // handler vetoed it. @p handled tells if a handler processed it.
  bool SendEvent(wxEventType type, int selection,
                 int oldSelection = wxNOT_FOUND, bool *handled = nullptr);
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
  // The last SetTheme() call. The art is made again from it when the system
  // colours change. Not used after SetArtProvider().
  wxTerminalTheme m_theme;
  wxColour m_outline;
  GlyphTabArt::Style m_artStyle{GlyphTabArt::Style::Pills};
  wxColour m_barColour;
  bool m_themed{false};
};

#endif // wxCHECK_VERSION(3, 3, 0)
