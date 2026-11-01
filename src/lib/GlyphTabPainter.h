#pragma once

#include "terminal_theme.h"
#include <map>
#include <tuple>
#include <vector>
#include <wx/bmpbndl.h>
#include <wx/colour.h>
#include <wx/dc.h>
#include <wx/dcclient.h>
#include <wx/font.h>
#include <wx/gdicmn.h>
#include <wx/window.h>

/**
 * @brief A button at the end of a tab (see GlyphTabInfo).
 */
struct GlyphTabButton {
  // The state bits.
  enum { kHover = 1, kPressed = 2 };

  // True for the close button, which the painter draws itself. Another button
  // is drawn with @p bitmap.
  bool close{false};
  // A mask of kHover and kPressed. A pressed button is also hovered.
  int state{0};
  wxBitmapBundle bitmap;
  // Where the button was drawn; empty if it is not shown.
  wxRect rect;
};

/**
 * @brief What GlyphTabPainter needs to draw and measure one tab.
 *
 * It is a plain struct. The painter gives back the rectangles after it draws
 * the tab.
 */
struct GlyphTabInfo {
  wxString caption;
  wxString tooltip;
  wxBitmapBundle bitmap;
  bool active{false};
  // The tab is under the mouse.
  bool hover{false};
  // The buttons at the end of the tab, usually the close button.
  std::vector<GlyphTabButton> buttons;
  // Where the tab was drawn.
  wxRect rect;
};

/**
 * @brief What GlyphTabPainter needs to draw and measure the header of a group
 *        of tabs.
 *
 * The header looks like a tab with the colour of the group.
 */
struct GlyphGroupInfo {
  wxString name;
  wxColour colour;
  bool collapsed{false};
  // The header is under the mouse.
  bool hover{false};
  // Where the header was drawn.
  wxRect rect;
};

/**
 * @brief Draws tabs with the colours of the terminal theme.
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
 * The painter draws the tabs of a GlyphTabBar. With wxWidgets 3.3 and later,
 * GlyphTabArt uses it to draw the tabs of a wxAuiNotebook. The shapes are
 * anti-aliased when a wxGraphicsContext is available.
 *
 * The painter does not follow the system colours. To change the colours, for
 * example after a theme change, make a new painter.
 */
class GlyphTabPainter {
public:
  enum class Style { Browser, Pills };

  explicit GlyphTabPainter(const wxTerminalTheme &theme,
                           Style style = Style::Pills);
  ~GlyphTabPainter() = default;
  GlyphTabPainter(const GlyphTabPainter &other) = default;
  GlyphTabPainter &operator=(const GlyphTabPainter &other) = default;

  // The font of the labels.
  void SetFont(const wxFont &font) { m_font = font; }
  const wxFont &GetFont() const { return m_font; }

  // The colours come from the theme: these only change the text colours.
  void SetNormalColour(const wxColour &colour);
  void SetActiveColour(const wxColour &colour);
  const wxColour &GetActiveColour() const { return m_fgActive; }
  const wxColour &GetDimmedColour() const { return m_fgDimmed; }
  // The outline of the current tab: all around a pill, and on the outer sides
  // of a Browser tab. The default is the magenta of the theme.
  void SetOutlineColour(const wxColour &colour) { m_outline = colour; }
  // The colour behind the tabs. The hover colour is derived from it; the
  // current tab stays the terminal background.
  void SetBarColour(const wxColour &colour);

  Style GetStyle() const { return m_style; }

  // The colour of a group that has no valid colour. It is also the first colour
  // of the palette of GlyphBook.
  static wxColour DefaultGroupColour() { return wxColour(0x5f, 0x63, 0x68); }

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
  // The header of a group: a chip with the colour and the name of the group,
  // in the place of a tab. Sets group.rect.
  void DrawGroupHeader(wxDC &dc, wxWindow *wnd, GlyphGroupInfo &group,
                       const wxRect &rect, wxDirection inner);
  // The line of the colour of a group along the side of the bar that is next to
  // the pages, over the header and the tabs of the group. Draw it after the
  // tabs, or what a tab draws (for example the hover) covers it. @p span is the
  // part of the bar they take.
  void DrawGroupLine(wxDC &dc, wxWindow *wnd, const wxRect &span,
                     const wxColour &colour, wxDirection inner);
  // The size of the header of a group in a horizontal bar.
  wxSize GetGroupHeaderSize(wxWindow *wnd, const GlyphGroupInfo &group);
  // The height of the header of a group in a vertical bar.
  int GetVerticalGroupHeaderHeight(wxWindow *wnd, const GlyphGroupInfo &group);
  // The size of a tab in a horizontal bar.
  wxSize GetTabSize(wxWindow *wnd, const GlyphTabInfo &tab);
  // The height of a row of a vertical tab bar.
  int GetVerticalTabHeight(wxWindow *wnd, const GlyphTabInfo &tab);

private:
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
  // The size of a button, and the width of all the buttons of @p tab (0
  // without buttons).
  wxSize ButtonSize(wxWindow *wnd, const GlyphTabButton &button) const;
  int GetButtonsWidth(wxWindow *wnd, const GlyphTabInfo &tab) const;
  // The size of the triangle on the header of a group, for a chip that is
  // @p chipHeight high.
  int IndicatorSize(wxWindow *wnd, int chipHeight) const;
  // The triangle as a bitmap of @p size pixels, drawn from an SVG that is made
  // in memory. It is empty without SVG support.
  wxBitmap GetIndicatorBitmap(bool collapsed, const wxColour &colour, int size);
  // The size of the icon, the label and the buttons, without the padding.
  wxSize GetContentSize(wxWindow *wnd, const GlyphTabInfo &tab) const;

  Style m_style;
  wxFont m_font;
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
  // The triangles that were made: by collapsed, colour and size.
  std::map<std::tuple<bool, unsigned long, int>, wxBitmap> m_indicatorCache;
};
