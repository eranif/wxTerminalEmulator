#pragma once

// The tab art of the notebook of GlyphT. It needs wxAuiTabArtBase, which is new
// in wxWidgets 3.3; with an older wxWidgets the notebook keeps its default art.
#include <wx/version.h>

#if wxCHECK_VERSION(3, 3, 0)

#include "terminal_theme.h"
#include <wx/aui/auibook.h>
#include <wx/aui/tabart.h>

/**
 * @brief Tabs painted with the colours of the terminal theme.
 *
 * There are two looks (see Style). With both, the other tabs have no
 * background and dimmed text, a tab under the mouse is highlighted, and the
 * close button is shown on the current tab and on the tab under the mouse.
 *
 * - Browser: the tab bar is darker than the terminal. The current tab has the
 *   terminal background and rounded top corners, so it looks like a part of the
 *   terminal below it.
 * - Pills: the tab bar has the colour of the terminal. Each tab is a small
 *   rounded rectangle with a gap between them, and the current one is filled
 *   and outlined (see SetOutlineColour()).
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
  // The outline of the current pill. The default is the magenta of the theme.
  void SetOutlineColour(const wxColour &colour) { m_outline = colour; }

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

  Style m_style;
  wxColour m_bgBar;    // Behind the tabs.
  wxColour m_bgActive; // The current tab.
  wxColour m_bgHover;  // A tab under the mouse.
  wxColour m_outline;  // Around the current pill.
  wxColour m_fgActive; // Text of the current tab.
  wxColour m_fgNormal; // Text of the other tabs.
  wxColour m_fgDimmed; // Buttons of the other tabs.
};

#endif // wxCHECK_VERSION(3, 3, 0)
