#pragma once

// The tab art for a wxAuiNotebook. It needs wxAuiTabArtBase, which is new in
// wxWidgets 3.3; GlyphBook and GlyphTabBar do not need it (see
// GlyphTabPainter).
#include <wx/version.h>

#if wxCHECK_VERSION(3, 3, 0)

#include "GlyphTabPainter.h"
#include <wx/aui/auibook.h>
#include <wx/aui/tabart.h>

/**
 * @brief Tabs of a wxAuiNotebook (at the top or at the bottom), painted with
 *        the colours of a terminal theme.
 *
 * This is the adapter between wxAuiNotebook and GlyphTabPainter, which does the
 * drawing: see there for the looks and the colours. Give it to a notebook with
 * wxAuiNotebook::SetArtProvider().
 *
 * The art does not follow the system colours. To change the colours, for
 * example after a theme change, give the notebook a new art.
 */
class GlyphTabArt : public wxAuiTabArtBase {
public:
  using Style = GlyphTabPainter::Style;

  explicit GlyphTabArt(const wxTerminalTheme &theme,
                       Style style = Style::Pills);
  ~GlyphTabArt() override = default;

  wxNODISCARD wxAuiTabArt *Clone() override;

  // The colours come from the theme: these only change the text colours.
  void SetColour(const wxColour &colour) override;
  void SetActiveColour(const wxColour &colour) override;
  // The outline of the current tab: all around a pill, and on the outer sides
  // of a Browser tab. The default is the magenta of the theme.
  void SetOutlineColour(const wxColour &colour);
  // The colour behind the tabs. The hover colour is derived from it; the
  // current tab stays the terminal background.
  void SetBarColour(const wxColour &colour);
  // The height of an icon in every tab (see
  // GlyphTabPainter::SetReservedIconSize()).
  void SetReservedIconSize(int size) { m_painter.SetReservedIconSize(size); }
  // The space above and below the label of a tab, in DIPs, which sets the
  // height of the tabs (see GlyphTabPainter::SetPaddingY()).
  void SetPaddingY(std::optional<int> dip) { m_painter.SetPaddingY(dip); }
  // Own colours for some tabs, for example to mark a tab that needs attention.
  // The art calls @p provider with the window of the page each time it draws a
  // tab, and draws the tab with the colours it returns (see GlyphColourPair
  // and GlyphTabInfo::colours). std::nullopt, or no provider (the default),
  // uses the normal colours. After the answer for a page changes, refresh the
  // notebook. A new art does not keep the provider of the old one.
  void SetColourProvider(GlyphTabColourProvider provider) {
    m_colourProvider = std::move(provider);
  }

  void DrawBackground(wxDC &dc, wxWindow *wnd, const wxRect &rect) override;
  int DrawPageTab(wxDC &dc, wxWindow *wnd, wxAuiNotebookPage &page,
                  const wxRect &rect) override;
  wxSize GetPageTabSize(wxReadOnlyDC &dc, wxWindow *wnd,
                        const wxAuiNotebookPage &page,
                        int *xExtent = nullptr) override;
  int GetBestTabCtrlSize(wxWindow *wnd, const wxAuiNotebookPageArray &pages,
                         const wxSize &requiredBmpSize) override;
  int GetIndentSize() override { return 0; }

  // The colours are not taken from the system, only the buttons are built
  // again.
  void UpdateColoursFromSystem() override { InitBitmaps(); }

private:
  GlyphTabArt(const GlyphTabArt &other) = default;

  wxColour GetButtonColour(wxAuiButtonId button,
                           wxAuiPaneButtonState state) const override;
  // The data of a page of a wxAuiNotebook.
  GlyphTabInfo InfoOf(const wxAuiNotebookPage &page) const;

  GlyphTabPainter m_painter;
  GlyphTabColourProvider m_colourProvider;
};

#endif // wxCHECK_VERSION(3, 3, 0)
