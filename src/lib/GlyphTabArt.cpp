#include "GlyphTabArt.h"

#if wxCHECK_VERSION(3, 3, 0)

#include <algorithm>
#include <memory>
#include <wx/control.h>
#include <wx/dc.h>
#include <wx/dcclient.h>
#include <wx/graphics.h>
#include <wx/window.h>

namespace {

// Space in DIPs.
constexpr int kMargin = 4;
constexpr int kPaddingX = 12;
// The right padding of a tab with buttons, so the close button sits near the
// end. A pill's round end has a radius of half its height, and the circle of
// the button still fits inside it.
constexpr int kButtonPaddingX = 6;
// The circle around the close button is this much wider than the button.
constexpr int kCloseCircleGrow = 4;
constexpr int kPaddingY = 6;
// The space between the edge of the bar and the tab.
constexpr int kTopGap = 4;
// The radius of the corners of the current tab and of the highlight.
constexpr int kTabRadius = 8;
constexpr int kHoverRadius = 6;
// Pills: the space around a pill, which is the gap between two pills.
constexpr int kPillGapX = 3;
constexpr int kPillGapY = 4;

// The colour @p t (0 to 1) of the way from @p a to @p b.
wxColour Mix(const wxColour &a, const wxColour &b, double t) {
  const auto mix = [t](unsigned char x, unsigned char y) {
    return static_cast<unsigned char>(x + (y - x) * t + 0.5);
  };
  return wxColour(mix(a.Red(), b.Red()), mix(a.Green(), b.Green()),
                  mix(a.Blue(), b.Blue()));
}

// The close button: an X in the middle of @p rect, and with @p circle valid, a
// filled circle around it. The X is drawn, not taken from the bitmap, so it is
// in the centre of the circle. Both are anti-aliased (a small wxDC circle is
// jagged on Windows); without a graphics context, @p fallback is drawn.
// The circle is @p circleGrow pixels wider than @p rect; the X keeps its size.
void DrawCloseButton(wxDC &dc, const wxRect &rect, const wxColour &circle,
                     int circleGrow, const wxColour &cross,
                     const wxBitmap &fallback) {
  std::unique_ptr<wxGraphicsContext> gc{
      wxGraphicsContext::CreateFromUnknownDC(dc)};
  if (!gc) {
    if (circle.IsOk()) {
      dc.SetBrush(circle);
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.DrawEllipse(rect.Inflate(circleGrow / 2, circleGrow / 2));
    }
    dc.DrawBitmap(fallback, rect.GetPosition(), true);
    return;
  }

  const double diameter = std::min(rect.width, rect.height);
  const double cx = rect.x + rect.width / 2.0;
  const double cy = rect.y + rect.height / 2.0;
  if (circle.IsOk()) {
    const double circleDiameter = diameter + circleGrow;
    gc->SetBrush(wxBrush(circle));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawEllipse(cx - circleDiameter / 2, cy - circleDiameter / 2,
                    circleDiameter, circleDiameter);
  }
  // The X takes a little less than half of the button.
  const double half = diameter * 0.2;
  gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(cross)
                               .Width(std::max(1.0, diameter / 10))
                               .Cap(wxCAP_ROUND)));
  gc->StrokeLine(cx - half, cy - half, cx + half, cy + half);
  gc->StrokeLine(cx - half, cy + half, cx + half, cy - half);
}

} // namespace

GlyphTabArt::GlyphTabArt(const wxTerminalTheme &theme, Style style)
    : wxAuiTabArtBase(), m_style(style), m_fgActive(theme.fg),
      m_fgNormal(Mix(theme.bg, theme.fg, 0.6)),
      m_fgDimmed(Mix(theme.bg, theme.fg, 0.4)) {
  if (m_style == Style::Pills) {
    // The bar looks like the terminal, the pills are lighter.
    m_bgBar = theme.bg;
    m_bgActive = Mix(theme.bg, theme.fg, 0.16);
    m_bgHover = Mix(theme.bg, theme.fg, 0.09);
  } else {
    // The bar is darker than the terminal, like the bar of a browser.
    const double luminance = 0.299 * theme.bg.Red() + 0.587 * theme.bg.Green() +
                             0.114 * theme.bg.Blue();
    m_bgBar = Mix(theme.bg, *wxBLACK, luminance < 128.0 ? 0.35 : 0.08);
    m_bgActive = theme.bg;
    m_bgHover = Mix(m_bgBar, theme.fg, 0.12);
  }
  InitBitmaps();
}

wxAuiTabArt *GlyphTabArt::Clone() { return new GlyphTabArt(*this); }

void GlyphTabArt::SetColour(const wxColour &colour) { m_fgNormal = colour; }

void GlyphTabArt::SetActiveColour(const wxColour &colour) {
  m_fgActive = colour;
  InitBitmaps();
}

wxColour GlyphTabArt::GetButtonColour(wxAuiButtonId WXUNUSED(button),
                                      wxAuiPaneButtonState state) const {
  return state == wxAUI_BUTTON_STATE_DISABLED ? m_fgDimmed : m_fgActive;
}

void GlyphTabArt::DrawBackground(wxDC &dc, wxWindow *WXUNUSED(wnd),
                                 const wxRect &rect) {
  dc.SetBrush(m_bgBar);
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.DrawRectangle(rect);
}

int GlyphTabArt::DrawPageTab(wxDC &dc, wxWindow *wnd, wxAuiNotebookPage &page,
                             const wxRect &rect) {
  // Clip everything to the rectangle we got.
  wxDCClipper clip(dc, rect);

  int xExtent = 0;
  const wxSize size = GetPageTabSize(dc, wnd, page, &xExtent);
  page.rect = wxRect(rect.GetPosition(), size);

  // The part of the tab that is drawn is "shape". Browser: the tab is a little
  // away from the edge of the bar, and the content is below (or above, with
  // wxAUI_NB_BOTTOM) the gap. Pills: there is a gap around the pill.
  const bool bottom = (m_flags & wxAUI_NB_BOTTOM) != 0;
  wxRect shape = page.rect;
  if (m_style == Style::Pills) {
    shape.Deflate(wnd->FromDIP(kPillGapX), wnd->FromDIP(kPillGapY));
    if (page.active || page.hover) {
      dc.SetBrush(page.active ? m_bgActive : m_bgHover);
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.DrawRoundedRectangle(shape, shape.height / 2.0);
    }
  } else {
    const int gap = wnd->FromDIP(kTopGap);
    shape.height -= gap;
    if (!bottom) {
      shape.y += gap;
    }
    if (page.active) {
      // Rounded corners on the side of the bar. The other corners are made
      // outside of the clipping rectangle, so the tab goes into the terminal.
      const int radius = wnd->FromDIP(kTabRadius);
      wxRect r = shape;
      r.height += radius;
      if (bottom) {
        r.y -= radius;
      }
      dc.SetBrush(m_bgActive);
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.DrawRoundedRectangle(r, radius);
    } else if (page.hover) {
      wxRect r = shape;
      r.Deflate(wnd->FromDIP(2), wnd->FromDIP(2));
      dc.SetBrush(m_bgHover);
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.DrawRoundedRectangle(r, wnd->FromDIP(kHoverRadius));
    }
  }

  // The icon, if any.
  int xStart = shape.x + wnd->FromDIP(kPaddingX);
  if (page.bitmap.IsOk()) {
    const wxBitmap bmp = page.bitmap.GetBitmapFor(wnd);
    const wxSize bitmapSize = bmp.GetLogicalSize();
    dc.DrawBitmap(bmp, xStart, shape.y + (shape.height - bitmapSize.y) / 2,
                  true);
    xStart += bitmapSize.x + wnd->FromDIP(kMargin);
  }

  // The buttons, at the right end. Their room is always kept, so the width of
  // a tab does not change when the mouse moves over it, but they are drawn only
  // on the current tab and on the tab under the mouse.
  int buttonsWidth = 0;
  for (const auto &button : page.buttons) {
    if (const wxBitmapBundle *bb = GetButtonBitmapBundle(button)) {
      buttonsWidth += bb->GetBitmapFor(wnd).GetLogicalWidth();
    }
  }

  const bool showButtons = page.active || page.hover;
  int xEnd = shape.x + shape.width -
             wnd->FromDIP(buttonsWidth ? kButtonPaddingX : kPaddingX);
  if (buttonsWidth) {
    xEnd -= buttonsWidth;
    int buttonX = xEnd;
    for (auto &button : page.buttons) {
      const wxBitmapBundle *bb = GetButtonBitmapBundle(button);
      if (!bb) {
        continue;
      }
      const wxBitmap bmp = bb->GetBitmapFor(wnd);
      const wxSize buttonSize = bmp.GetLogicalSize();
      if (!showButtons) {
        // An empty rectangle cannot be clicked.
        button.rect = wxRect();
        buttonX += buttonSize.x;
        continue;
      }
      button.rect = wxRect(buttonX, shape.y + (shape.height - buttonSize.y) / 2,
                           buttonSize.x, buttonSize.y);
      if (button.curState == wxAUI_BUTTON_STATE_PRESSED) {
        // Move a pressed button a little, so it looks pressed.
        button.rect.Offset(1, 1);
      }
      if (button.id == wxAUI_BUTTON_CLOSE) {
        // The X is dimmed; while the mouse is over it, it is in the text
        // colour, inside a circle.
        const bool hot = button.curState == wxAUI_BUTTON_STATE_HOVER ||
                         button.curState == wxAUI_BUTTON_STATE_PRESSED;
        const wxColour &tabBg = page.active ? m_bgActive : m_bgHover;
        DrawCloseButton(dc, button.rect,
                        hot ? Mix(tabBg, m_fgActive, 0.25) : wxNullColour,
                        wnd->FromDIP(kCloseCircleGrow),
                        hot ? m_fgActive : m_fgDimmed, bmp);
      } else {
        dc.DrawBitmap(bmp, button.rect.GetPosition(), true);
      }
      buttonX += buttonSize.x;
    }
    xEnd -= wnd->FromDIP(kMargin);
  }

  // The text.
  // The current tab uses the normal font too: AUI makes the selected font
  // bold, and the bold measuring font would make every tab wider.
  dc.SetFont(m_normalFont);
  dc.SetTextForeground(page.active || page.hover ? m_fgActive : m_fgNormal);
  // The width of the tab comes from GetTextExtent(). Ellipsize() adds up the
  // width of each character, which on Windows is a few pixels more, so it would
  // cut a label that has all the room it measured. Use it only when the label
  // really does not fit.
  const int textRoom = std::max(xEnd - xStart, 0);
  wxString text = page.caption;
  if (dc.GetTextExtent(text).x > textRoom) {
    text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, textRoom);
  }
  const int textHeight = dc.GetTextExtent(text).y;
  dc.DrawText(text, xStart, shape.y + (shape.height - textHeight) / 2);

  return xExtent;
}

wxSize GlyphTabArt::GetPageTabSize(wxReadOnlyDC &dc, wxWindow *wnd,
                                   const wxAuiNotebookPage &page,
                                   int *xExtent) {
  dc.SetFont(m_normalFont);
  wxSize size = dc.GetTextExtent(page.caption);

  bool firstButton = true;
  for (const auto &button : page.buttons) {
    const wxBitmapBundle *bb = GetButtonBitmapBundle(button);
    if (!bb) {
      continue;
    }
    if (firstButton) {
      size.x += wnd->FromDIP(kMargin);
      firstButton = false;
    }
    size.x += bb->GetBitmapFor(wnd).GetLogicalWidth();
  }

  if (page.bitmap.IsOk()) {
    const wxSize bitmapSize = page.bitmap.GetPreferredLogicalSizeFor(wnd);
    size.x += bitmapSize.x + wnd->FromDIP(kMargin);
    size.IncTo(bitmapSize);
  }

  // Each padding converted on its own, not the conversion of the sum: the
  // drawing uses each padding once per side, and the two can differ by a pixel
  // with a scale like 105% or 150%. With buttons, the right one is smaller.
  size.x += wnd->FromDIP(kPaddingX) +
            wnd->FromDIP(firstButton ? kPaddingX : kButtonPaddingX);
  size.y += 2 * wnd->FromDIP(kPaddingY);
  if (m_style == Style::Pills) {
    size.x += 2 * wnd->FromDIP(kPillGapX);
    size.y += 2 * wnd->FromDIP(kPillGapY);
  } else {
    size.y += wnd->FromDIP(kTopGap);
  }

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
  wxInfoDC dc(wnd);
  dc.SetFont(m_normalFont);

  wxBitmapBundle measureBmp;
  if (requiredBmpSize.IsFullySpecified()) {
    measureBmp = wxBitmap(requiredBmpSize);
  }

  wxSize size;
  // A copy of each page: it is changed below.
  for (auto page : pages) {
    if (measureBmp.IsOk()) {
      page.bitmap = measureBmp;
    }
    page.active = true;
    size.IncTo(GetPageTabSize(dc, wnd, page));
  }
  return size.y;
}

#endif // wxCHECK_VERSION(3, 3, 0)
