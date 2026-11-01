#include "GlyphTabArt.h"

#if wxCHECK_VERSION(3, 3, 0)

#include <algorithm>

GlyphTabArt::GlyphTabArt(const wxTerminalTheme &theme, Style style)
    : wxAuiTabArtBase(), m_painter(theme, style) {
  InitBitmaps();
}

wxAuiTabArt *GlyphTabArt::Clone() { return new GlyphTabArt(*this); }

void GlyphTabArt::SetColour(const wxColour &colour) {
  m_painter.SetNormalColour(colour);
}

void GlyphTabArt::SetActiveColour(const wxColour &colour) {
  m_painter.SetActiveColour(colour);
  InitBitmaps();
}

void GlyphTabArt::SetOutlineColour(const wxColour &colour) {
  m_painter.SetOutlineColour(colour);
}

void GlyphTabArt::SetBarColour(const wxColour &colour) {
  m_painter.SetBarColour(colour);
}

wxColour GlyphTabArt::GetButtonColour(wxAuiButtonId WXUNUSED(button),
                                      wxAuiPaneButtonState state) const {
  return state == wxAUI_BUTTON_STATE_DISABLED ? m_painter.GetDimmedColour()
                                              : m_painter.GetActiveColour();
}

GlyphTabInfo GlyphTabArt::InfoOf(const wxAuiNotebookPage &page) const {
  GlyphTabInfo tab;
  tab.caption = page.caption;
  tab.tooltip = page.tooltip;
  tab.bitmap = page.bitmap;
  tab.active = page.active;
  tab.hover = page.hover;
  for (const wxAuiTabContainerButton &button : page.buttons) {
    GlyphTabButton info;
    info.close = button.id == wxAUI_BUTTON_CLOSE;
    if (button.curState & wxAUI_BUTTON_STATE_HOVER) {
      info.state |= GlyphTabButton::kHover;
    }
    if (button.curState & wxAUI_BUTTON_STATE_PRESSED) {
      info.state |= GlyphTabButton::kPressed;
    }
    if (!info.close) {
      if (const wxBitmapBundle *bundle = GetButtonBitmapBundle(button)) {
        info.bitmap = *bundle;
      }
    }
    tab.buttons.push_back(std::move(info));
  }
  return tab;
}

void GlyphTabArt::DrawBackground(wxDC &dc, wxWindow *wnd, const wxRect &rect) {
  m_painter.SetFont(m_normalFont);
  // The terminal is below the tabs, or above them with wxAUI_NB_BOTTOM.
  m_painter.DrawBarBackground(dc, wnd, rect,
                              (m_flags & wxAUI_NB_BOTTOM) ? wxTOP : wxBOTTOM);
}

int GlyphTabArt::DrawPageTab(wxDC &dc, wxWindow *wnd, wxAuiNotebookPage &page,
                             const wxRect &rect) {
  m_painter.SetFont(m_normalFont);
  // Clip everything to the rectangle we got.
  wxDCClipper clip(dc, rect);

  GlyphTabInfo tab = InfoOf(page);
  int xExtent = 0;
  const wxSize size = GetPageTabSize(dc, wnd, page, &xExtent);
  wxRect tabRect(rect.GetPosition(), size);
  // Browser: the tab reaches the terminal, so it uses the whole height of the
  // row that AUI gives, not the height of the tab. Otherwise a strip of the bar
  // shows between the tab and the page when the row is taller. The hit area
  // (page.rect) is the same, so the whole drawn tab can be clicked.
  if (m_painter.GetStyle() == Style::Browser) {
    tabRect.height = rect.height;
  }

  // The terminal is below the tabs, or above them with wxAUI_NB_BOTTOM.
  m_painter.DrawTab(dc, wnd, tab, tabRect,
                    (m_flags & wxAUI_NB_BOTTOM) ? wxTOP : wxBOTTOM);

  // The rectangles go back to AUI, for the hit test.
  page.rect = tab.rect;
  for (size_t i = 0; i < page.buttons.size() && i < tab.buttons.size(); ++i) {
    page.buttons[i].rect = tab.buttons[i].rect;
  }
  return xExtent;
}

wxSize GlyphTabArt::GetPageTabSize(wxReadOnlyDC &WXUNUSED(dc), wxWindow *wnd,
                                   const wxAuiNotebookPage &page,
                                   int *xExtent) {
  m_painter.SetFont(m_normalFont);
  wxSize size = m_painter.GetTabSize(wnd, InfoOf(page));
  if (m_flags & wxAUI_NB_TAB_FIXED_WIDTH) {
    size.x = m_fixedTabWidth;
  }
  if (xExtent) {
    *xExtent = size.x;
  }
  return size;
}

int GlyphTabArt::GetBestTabCtrlSize(wxWindow *wnd,
                                    const wxAuiNotebookPageArray &pages,
                                    const wxSize &requiredBmpSize) {
  m_painter.SetFont(m_normalFont);
  wxBitmapBundle measureBmp;
  if (requiredBmpSize.IsFullySpecified()) {
    measureBmp = wxBitmap(requiredBmpSize);
  }

  wxSize size;
  // A copy of each page: it is changed below.
  for (const auto &page : pages) {
    GlyphTabInfo tab = InfoOf(page);
    if (measureBmp.IsOk()) {
      tab.bitmap = measureBmp;
    }
    tab.active = true;
    size.IncTo(m_painter.GetTabSize(wnd, tab));
  }
  return size.y;
}

#endif // wxCHECK_VERSION(3, 3, 0)
