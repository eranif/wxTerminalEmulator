#pragma once

// A tabbed book with tabs on any side. It needs wxWidgets 3.3 (see
// GlyphTabArt); with an older wxWidgets use wxAuiNotebook.
#include <wx/version.h>

#if wxCHECK_VERSION(3, 3, 0)

#include "GlyphTabArt.h"
#include "GlyphTabBar.h"
#include "terminal_theme.h"
#include <functional>
#include <wx/aui/auibook.h>
#include <wx/bookctrl.h>

/**
 * @brief A book control with tabs at the top, at the bottom, on the left or on
 *        the right, painted with the colours of a terminal theme.
 *
 * It has the API and the events of a wxAuiNotebook. Inside, it is a
 * wxAuiNotebook (the pages) and, for the left and right sides, a GlyphTabBar
 * (wxAUI has no vertical tabs). Use it like the notebook:
 *
 * @code
 * auto *book = new GlyphBook(parent, wxID_ANY, wxDefaultPosition,
 *                            wxDefaultSize,
 *                            wxAUI_NB_DEFAULT_STYLE | wxAUI_NB_LEFT);
 * book->SetTheme(theme);
 * book->AddPage(new wxTextCtrl(book->GetPagesParent(), wxID_ANY), "One", true);
 * book->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &MyFrame::OnPage, this);
 * @endcode
 *
 * **Tab position.** The window style tells where the tabs are: wxAUI_NB_TOP,
 * wxAUI_NB_BOTTOM, wxAUI_NB_LEFT or wxAUI_NB_RIGHT (the last two are not
 * implemented by wxAuiNotebook). SetTabPosition() changes it at any time. With
 * tabs on the left or right the book cannot be split: wxAUI_NB_TAB_SPLIT,
 * wxAUI_NB_TAB_MOVE and wxAUI_NB_TAB_EXTERNAL_MOVE are ignored, and the pages
 * are put in one tab control. The other flags (the close buttons, the middle
 * click) work on all sides.
 *
 * **Events.** The wxEVT_AUINOTEBOOK_* events of the inner notebook are sent
 * again with the book as the event object and the id of the book, and then go
 * to the parents of the book, as with a wxAuiNotebook. Veto() and Allow() of
 * the handlers reach the notebook (a vetoed wxEVT_AUINOTEBOOK_PAGE_CLOSE keeps
 * the page). The events from the tab bar are the same.
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
  ~GlyphBook() override;

  bool Create(wxWindow *parent, wxWindowID id = wxID_ANY,
              const wxPoint &pos = wxDefaultPosition,
              const wxSize &size = wxDefaultSize,
              long style = wxAUI_NB_DEFAULT_STYLE,
              const wxString &name = wxS("GlyphBook"));

  // Appearance
  // ----------

  // Paint the tabs with the colours of @p theme. @p outline is the outline of
  // the current tab (the default is the magenta of the theme). Call it again
  // after a theme change. Until the first call the tabs use the dark theme.
  void SetTheme(const wxTerminalTheme &theme,
                const wxColour &outline = wxNullColour,
                GlyphTabArt::Style style = GlyphTabArt::Style::Pills);
  // Paint the tabs with @p art. The book takes it, like wxAuiNotebook does.
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
  wxWindow *GetPagesParent() const { return m_notebook; }
  // The notebook inside. Use it to read things that the book does not forward;
  // do not change its window style, its tab height or its tab art.
  wxAuiNotebook *GetNotebook() const { return m_notebook; }

  // Pages
  // -----

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
  int SetSelection(size_t page) override;
  int ChangeSelection(size_t page) override;

  // The index of the page whose tab is at @p pt (in the coordinates of the
  // book), or wxNOT_FOUND.
  int HitTest(const wxPoint &pt, long *flags = nullptr) const override;

  // Move the page to a new tab control on the side @p direction (wxLEFT,
  // wxRIGHT, wxTOP or wxBOTTOM). Does nothing with tabs on the left or right.
  void Split(size_t page, int direction);
  void UnsplitAll();

  // Save and load the order and the split of the tabs (see wxAuiNotebook).
  void SaveLayout(const wxString &name, wxAuiBookSerializer &serializer) const;
  void LoadLayout(const wxString &name, wxAuiBookDeserializer &deserializer);

protected:
  wxWindow *DoRemovePage(size_t page) override;
  wxSize DoGetBestSize() const override;
  void OnChildFocus(wxChildFocusEvent& event);

private:
  void Init();
  // The window style of the notebook inside.
  long GetNotebookStyle() const;
  // Show the tab bar or the tabs of the notebook, and put the bar on its side.
  void UpdateLayout();
  // Update the tab bar after a change that sends no notebook event.
  void SyncTabBar();
  // Send an event of the notebook again as an event of the book.
  void ForwardEvent(wxAuiNotebookEvent &event);
  void OnSize(wxSizeEvent &event);

  wxAuiNotebook *m_notebook{nullptr};
  GlyphTabBar *m_tabBar{nullptr};
  // The window style, with the flags the notebook does not know.
  long m_style{wxAUI_NB_DEFAULT_STYLE};
  int m_tabBarWidth{200};
};

#endif // wxCHECK_VERSION(3, 3, 0)
