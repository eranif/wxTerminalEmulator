#pragma once

// The tab art of the notebook of GlyphT. It needs wxAuiTabArtBase, which is new
// in wxWidgets 3.3; with an older wxWidgets the notebook keeps its default art.
#include <wx/version.h>

#if wxCHECK_VERSION(3, 3, 0)

#include "terminal_theme.h"
#include <vector>
#include <wx/aui/auibook.h>
#include <wx/aui/tabart.h>

/**
 * @brief What GlyphTabArt needs to draw and measure one tab.
 *
 * It is a plain struct, so tabs can be drawn without a wxAuiNotebook (see
 * GlyphTabBar). The art gives back the rectangles after it draws the tab.
 */
struct GlyphTabInfo {
  wxString caption;
  wxString tooltip;
  wxBitmapBundle bitmap;
  bool active{false};
  // The tab is under the mouse.
  bool hover{false};
  // The buttons at the end of the tab (the close button). Set curState to
  // show a hovered or pressed button.
  std::vector<wxAuiTabContainerButton> buttons;
  // Where the tab was drawn.
  wxRect rect;
};

/**
 * @brief Tabs painted with the colours of the terminal theme.
 *
 * There are two looks (see Style). With both, the other tabs have no
 * background and dimmed text, a tab under the mouse is highlighted, and the
 * close button is shown on the current tab and on the tab under the mouse.
 *
 * - Browser: the current tab has the terminal background, rounded corners and
 *   an outline on the outer sides, so it looks like a part of the terminal
 *   next to it and still stands out from the bar.
 * - Pills: each tab is a small rounded rectangle with a gap between them, and
 *   the current one is outlined (see SetOutlineColour()).
 *
 * The colours are the same in both styles: the bar, the current tab and the
 * hovered tab (see SetBarColour()). Only the shape and the outline differ.
 *
 * The art draws the tabs of a wxAuiNotebook (at the top or at the bottom), and
 * the tabs of a GlyphTabBar (see DrawTab()). The shapes are
 * anti-aliased when a wxGraphicsContext is available.
 *
 * The art does not follow the system colours. To change the colours, for
 * example after a theme change, give the notebook a new art with
 * wxAuiNotebook::SetArtProvider().
 */
class GlyphTabArt : public wxAuiTabArtBase {
public:
  enum class Style { Browser, Pills };

  explicit GlyphTabArt(const wxTerminalTheme &theme,
                       Style style = Style::Pills);
  ~GlyphTabArt() override = default;

  wxNODISCARD wxAuiTabArt *Clone() override;

  // The colours come from the theme: these only change the text colours.
  void SetColour(const wxColour &colour) override;
  void SetActiveColour(const wxColour &colour) override;
  // The outline of the current tab: all around a pill, and on the outer sides
  // of a Browser tab. The default is the magenta of the theme.
  void SetOutlineColour(const wxColour &colour) { m_outline = colour; }
  // The colour behind the tabs. The hover colour is derived from it; the
  // current tab stays the terminal background.
  void SetBarColour(const wxColour &colour);

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

  // The functions below draw and measure tabs without a wxAuiNotebook (see
  // GlyphTabBar). The ones above are for a wxAuiNotebook, and use these.

  /**
   * Draw a tab in @p rect. @p inner is the side of the tab next to the
   * terminal: wxBOTTOM for tabs above the terminal, wxTOP for tabs below it,
   * wxRIGHT for a bar on the left, wxLEFT for a bar on the right. Sets
   * tab.rect and the rectangles of the buttons of @p tab (an empty rectangle
   * for a button that is not shown).
   */
  void DrawTab(wxDC &dc, wxWindow *wnd, GlyphTabInfo &tab, const wxRect &rect,
               wxDirection inner);
  // The background of a tab bar (@p rect), with the line along its side
  // @p inner, next to the terminal.
  void DrawBarBackground(wxDC &dc, wxWindow *wnd, const wxRect &rect,
                         wxDirection inner);
  // The button that opens the list of the tabs, in @p rect. @p hot is true
  // while the mouse is on it.
  void DrawDropDownButton(wxDC &dc, wxWindow *wnd, const wxRect &rect,
                          bool hot);
  // The size of a tab in a horizontal bar.
  wxSize GetTabSize(wxReadOnlyDC &dc, wxWindow *wnd, const GlyphTabInfo &tab);
  // The height of a row of a vertical tab bar.
  int GetVerticalTabHeight(wxReadOnlyDC &dc, wxWindow *wnd,
                           const GlyphTabInfo &tab);

private:
  GlyphTabArt(const GlyphTabArt &other) = default;

  wxColour GetButtonColour(wxAuiButtonId button,
                           wxAuiPaneButtonState state) const override;
  // The data of a page of a wxAuiNotebook.
  static GlyphTabInfo InfoOf(const wxAuiNotebookPage &page);

  // The part of the row @p rect that is painted. @p inner is the side next to
  // the terminal.
  wxRect GetShapeRect(wxWindow *wnd, const wxRect &rect,
                      wxDirection inner) const;
  // The background of a tab: a pill, or a browser tab that goes into the
  // terminal on the side @p inner.
  void DrawShape(wxDC &dc, wxWindow *wnd, const GlyphTabInfo &tab,
                 const wxRect &shape, wxDirection inner) const;
  // The icon, the label and the buttons of a tab inside @p shape.
  void DrawContent(wxDC &dc, wxWindow *wnd, GlyphTabInfo &tab,
                   const wxRect &shape);
  // The buttons at the end of @p shape. Returns the x where they start (the
  // end of the room of the label), or the end of @p shape without buttons.
  int DrawButtons(wxDC &dc, wxWindow *wnd, GlyphTabInfo &tab,
                  const wxRect &shape);
  // The width of the buttons of @p tab, or 0 without buttons.
  int GetButtonsWidth(wxWindow *wnd, const GlyphTabInfo &tab) const;
  // The size of the icon, the label and the buttons, without the padding.
  wxSize GetContentSize(wxReadOnlyDC &dc, wxWindow *wnd,
                        const GlyphTabInfo &tab) const;

  Style m_style;
  wxColour m_bgBar;      // Behind the tabs.
  wxColour m_bgActive;   // The current tab.
  wxColour m_bgHover;    // A tab under the mouse.
  wxColour m_bgTerminal; // The terminal background, for the current tab.
  // The text colour of the theme, for the derived colours.
  wxColour m_fgTheme;
  wxColour m_outline;  // Around the current pill.
  wxColour m_fgActive; // Text of the current tab.
  wxColour m_fgNormal; // Text of the other tabs.
  wxColour m_fgDimmed; // Buttons of the other tabs.
  wxColour m_fgHover;  // Text of a tab under the mouse.
};

#endif // wxCHECK_VERSION(3, 3, 0)
