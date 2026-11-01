#include "GlyphTabPainter.h"

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
// The space between the label and the buttons.
constexpr int kButtonGap = 16;
constexpr int kPaddingX = 12;
// The right padding of a tab with buttons, so the close button sits near the
// end. The highlight of the button still fits inside the rounded corners.
constexpr int kButtonPaddingX = 6;
// The highlight around the close button is this much wider than the button.
constexpr int kCloseHighlightGrow = 4;
constexpr int kCloseHighlightRadius = 4;
// The size of the close button, in DIPs.
constexpr int kCloseSize = 16;
// The least width of the header of a group in a horizontal bar, in DIPs.
constexpr int kMinHeaderWidth = 36;
// The triangle on the header of a group tells if the group is collapsed or not.
// It is 50% of the height of the header, but not less than this, in DIPs. The
// space between it and the name is also in DIPs.
constexpr int kMinIndicatorSize = 8;
constexpr int kIndicatorGap = 8;
// The thickness of the line of a group, in DIPs.
constexpr int kGroupLineWidth = 3;
constexpr int kPaddingY = 6;
// Browser: the space between the outer edge of the bar and the tab.
constexpr int kTopGap = 4;
// The radius of the corners of the current tab and of the highlight.
constexpr int kTabRadius = 8;
// Pills: the space around a pill, which is the gap between two pills, and the
// radius of its corners.
constexpr int kPillGapX = 3;
constexpr int kPillGapY = 4;
constexpr int kPillRadius = 2;
// Pills in a vertical bar: the space between a pill and the sides of the bar,
// and above and below a pill.
constexpr int kVPillGapX = 6;
constexpr int kVPillGapY = 2;

// The corners of a rectangle that are rounded (see DrawRoundedRect()).
enum Corner {
  kTopLeft = 1,
  kTopRight = 2,
  kBottomRight = 4,
  kBottomLeft = 8,
  kAllCorners = kTopLeft | kTopRight | kBottomRight | kBottomLeft
};

// The colour @p t (0 to 1) of the way from @p a to @p b.
wxColour Mix(const wxColour &a, const wxColour &b, double t) {
  const auto mix = [t](unsigned char x, unsigned char y) {
    return static_cast<unsigned char>(x + (y - x) * t + 0.5);
  };
  return wxColour(mix(a.Red(), b.Red()), mix(a.Green(), b.Green()),
                  mix(a.Blue(), b.Blue()));
}

// The colour @p t (0 to 1) of the way from @p bg to black or to white, the one
// that is far from the brightness of @p bg. Text in it is readable on @p bg,
// whatever the theme text colour is.
wxColour TextOn(const wxColour &bg, double t) {
  const double luminance =
      0.299 * bg.Red() + 0.587 * bg.Green() + 0.114 * bg.Blue();
  return Mix(bg, luminance < 128.0 ? *wxWHITE : *wxBLACK, t);
}

// The strip of @p width pixels along the side @p inner of @p rect: the side
// next to the terminal (see DrawPageTab()).
wxRect InnerStrip(const wxRect &rect, wxDirection inner, int width) {
  switch (inner) {
  case wxBOTTOM:
    return wxRect(rect.x, rect.GetBottom() + 1 - width, rect.width, width);
  case wxTOP:
    return wxRect(rect.x, rect.y, rect.width, width);
  case wxRIGHT:
    return wxRect(rect.GetRight() + 1 - width, rect.y, width, rect.height);
  default:
    return wxRect(rect.x, rect.y, width, rect.height);
  }
}

bool IsHorizontal(wxDirection inner) {
  return inner == wxTOP || inner == wxBOTTOM;
}

// A rectangle with rounded @p corners (a mask of Corner) of @p radius.
void AddRoundedRect(wxGraphicsPath &path, double x, double y, double w,
                    double h, double radius, int corners) {
  const double r = std::min({radius, w / 2, h / 2});
  const auto rr = [&](int corner) { return (corners & corner) ? r : 0.0; };
  path.MoveToPoint(x + rr(kTopLeft), y);
  path.AddLineToPoint(x + w - rr(kTopRight), y);
  if (corners & kTopRight) {
    path.AddArcToPoint(x + w, y, x + w, y + r, r);
  }
  path.AddLineToPoint(x + w, y + h - rr(kBottomRight));
  if (corners & kBottomRight) {
    path.AddArcToPoint(x + w, y + h, x + w - r, y + h, r);
  }
  path.AddLineToPoint(x + rr(kBottomLeft), y + h);
  if (corners & kBottomLeft) {
    path.AddArcToPoint(x, y + h, x, y + h - r, r);
  }
  path.AddLineToPoint(x, y + rr(kTopLeft));
  if (corners & kTopLeft) {
    path.AddArcToPoint(x, y, x + r, y, r);
  }
  path.CloseSubpath();
}

// Fill @p rect with @p fill, with the @p corners (a mask of Corner) rounded
// with @p radius. With @p outline valid, a line of @p outlineWidth pixels is
// drawn inside the edge. The shape is anti-aliased (wxDC curves are jagged on
// Windows); without a graphics context it is drawn with the wxDC.
void DrawRoundedRect(wxDC &dc, const wxRect &rect, int radius, int corners,
                     const wxColour &fill, const wxColour &outline = {},
                     int outlineWidth = 1) {
  std::unique_ptr<wxGraphicsContext> gc{
      wxGraphicsContext::CreateFromUnknownDC(dc)};
  if (!gc) {
    dc.SetBrush(fill);
    dc.SetPen(outline.IsOk() ? wxPen(outline, outlineWidth)
                             : *wxTRANSPARENT_PEN);
    // The corners that are not rounded are drawn outside of the clipping
    // rectangle.
    wxRect r = rect;
    if (!(corners & (kTopLeft | kTopRight))) {
      r.y -= radius;
      r.height += radius;
    }
    if (!(corners & (kBottomLeft | kBottomRight))) {
      r.height += radius;
    }
    if (!(corners & (kTopLeft | kBottomLeft))) {
      r.x -= radius;
      r.width += radius;
    }
    if (!(corners & (kTopRight | kBottomRight))) {
      r.width += radius;
    }
    wxDCClipper clip(dc, rect);
    dc.DrawRoundedRectangle(r, radius);
    return;
  }

  gc->SetBrush(wxBrush(fill));
  double inset = 0;
  if (outline.IsOk()) {
    gc->SetPen(wxPen(outline, outlineWidth));
    // The line is centred on the path: keep it inside the rectangle, and
    // sharp.
    inset = outlineWidth / 2.0;
  } else {
    gc->SetPen(*wxTRANSPARENT_PEN);
  }
  wxGraphicsPath path = gc->CreatePath();
  AddRoundedRect(path, rect.x + inset, rect.y + inset, rect.width - 2 * inset,
                 rect.height - 2 * inset, radius, corners);
  gc->DrawPath(path);
}

// The close button: an X in the middle of @p rect, and with @p highlight
// valid, a filled rounded square around it. Both are anti-aliased (small
// wxDC curves are jagged on Windows); without a graphics context they are
// drawn with the wxDC. The square is @p grow pixels wider than @p rect, with
// corners of
// @p radius; the X keeps its size.
void DrawCloseButton(wxDC &dc, const wxRect &rect, const wxColour &highlight,
                     int grow, int radius, const wxColour &cross) {
  std::unique_ptr<wxGraphicsContext> gc{
      wxGraphicsContext::CreateFromUnknownDC(dc)};
  if (!gc) {
    if (highlight.IsOk()) {
      dc.SetBrush(highlight);
      dc.SetPen(*wxTRANSPARENT_PEN);
      dc.DrawRoundedRectangle(rect.Inflate(grow / 2, grow / 2), radius);
    }
    const int cx = rect.x + rect.width / 2;
    const int cy = rect.y + rect.height / 2;
    const int half = std::max(1, std::min(rect.width, rect.height) / 5);
    dc.SetPen(
        wxPen(cross, std::max(1, std::min(rect.width, rect.height) / 10)));
    dc.DrawLine(cx - half, cy - half, cx + half, cy + half);
    dc.DrawLine(cx - half, cy + half, cx + half, cy - half);
    return;
  }

  const double diameter = std::min(rect.width, rect.height);
  const double cx = rect.x + rect.width / 2.0;
  const double cy = rect.y + rect.height / 2.0;
  if (highlight.IsOk()) {
    const double side = diameter + grow;
    gc->SetBrush(wxBrush(highlight));
    gc->SetPen(*wxTRANSPARENT_PEN);
    gc->DrawRoundedRectangle(cx - side / 2, cy - side / 2, side, side, radius);
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

GlyphTabPainter::GlyphTabPainter(const wxTerminalTheme &theme, Style style)
    : m_style(style), m_font(*wxNORMAL_FONT), m_fgActive(theme.fg) {
  m_bgTerminal = theme.bg;
  m_fgTheme = theme.fg;
  m_outline = theme.magenta;
  if (m_style == Style::Pills) {
    // The bar looks like the terminal.
    SetBarColour(theme.bg);
  } else {
    // The bar is darker than the terminal, like the bar of a browser.
    const double luminance = 0.299 * theme.bg.Red() + 0.587 * theme.bg.Green() +
                             0.114 * theme.bg.Blue();
    SetBarColour(Mix(theme.bg, *wxBLACK, luminance < 128.0 ? 0.35 : 0.08));
  }
}

void GlyphTabPainter::SetBarColour(const wxColour &colour) {
  // Both styles use the same colours: the current tab is the terminal
  // background, and a hovered tab is a mix of the bar and the text colour.
  // Only the shape and the outline differ.
  m_bgBar = colour;
  m_bgActive = m_bgTerminal;
  m_bgHover = Mix(colour, m_fgTheme, 0.12);
  // The text of the other tabs and their buttons is drawn on the bar, and the
  // text of a hovered tab on the hover colour. Their colours are chosen for
  // contrast with that background: a light text on a dark bar and a dark text
  // on a light bar, whatever the text colour of the theme is.
  m_fgNormal = TextOn(colour, 0.6);
  m_fgDimmed = TextOn(colour, 0.4);
  m_fgHover = TextOn(m_bgHover, 0.85);
}

void GlyphTabPainter::SetNormalColour(const wxColour &colour) {
  m_fgNormal = colour;
}

void GlyphTabPainter::SetActiveColour(const wxColour &colour) {
  m_fgActive = colour;
}

void GlyphTabPainter::DrawBarBackground(wxDC &dc, wxWindow *wnd,
                                        const wxRect &rect, wxDirection inner) {
  dc.SetBrush(m_bgBar);
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.DrawRectangle(rect);
  // A line along the side next to the pages. The current tab covers it (see
  // DrawShape()), so it is not drawn under it. Pills have it only in a
  // vertical bar.
  if (m_style == Style::Browser || !IsHorizontal(inner)) {
    dc.SetBrush(m_style == Style::Pills ? m_bgHover : m_outline);
    dc.DrawRectangle(InnerStrip(rect, inner, std::max(1, wnd->FromDIP(1))));
  }
}

void GlyphTabPainter::DrawTab(wxDC &dc, wxWindow *wnd, GlyphTabInfo &tab,
                              const wxRect &rect, wxDirection inner) {
  wxDCClipper clip(dc, rect);
  tab.rect = rect;
  const wxRect shape = GetShapeRect(wnd, rect, inner);
  DrawShape(dc, wnd, tab, shape, inner);
  DrawContent(dc, wnd, tab, shape);
}

void GlyphTabPainter::DrawDropDownButton(wxDC &dc, wxWindow *wnd,
                                         const wxRect &rect, bool hot) {
  if (hot) {
    wxRect box = rect;
    box.Deflate(wnd->FromDIP(3), wnd->FromDIP(5));
    DrawRoundedRect(dc, box, wnd->FromDIP(kCloseHighlightRadius), kAllCorners,
                    m_bgHover);
  }
  const wxColour &fg = hot ? m_fgHover : m_fgNormal;
  const double cx = rect.x + rect.width / 2.0;
  const double cy = rect.y + rect.height / 2.0;
  const double half = wnd->FromDIP(4);
  std::unique_ptr<wxGraphicsContext> gc{
      wxGraphicsContext::CreateFromUnknownDC(dc)};
  if (gc) {
    gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(fg)
                                 .Width(std::max(1.0, wnd->FromDIP(1) * 1.4))
                                 .Cap(wxCAP_ROUND)
                                 .Join(wxJOIN_ROUND)));
    wxGraphicsPath path = gc->CreatePath();
    path.MoveToPoint(cx - half, cy - half / 2);
    path.AddLineToPoint(cx, cy + half / 2);
    path.AddLineToPoint(cx + half, cy - half / 2);
    gc->StrokePath(path);
    return;
  }
  dc.SetPen(wxPen(fg, std::max(1, wnd->FromDIP(1))));
  dc.DrawLine(int(cx - half), int(cy - half / 2), int(cx), int(cy + half / 2));
  dc.DrawLine(int(cx), int(cy + half / 2), int(cx + half), int(cy - half / 2));
}

// The row @p rect without the gap on its outer side (the side away from the
// terminal): the shape of a tab of the Browser style, which touches the
// terminal on the inner side.
static wxRect WithoutOuterGap(wxWindow *wnd, const wxRect &rect,
                              wxDirection inner) {
  wxRect shape = rect;
  const int gap = wnd->FromDIP(kTopGap);
  switch (inner) {
  case wxBOTTOM:
    shape.y += gap;
    shape.height -= gap;
    break;
  case wxTOP:
    shape.height -= gap;
    break;
  case wxRIGHT:
    shape.x += gap;
    shape.width -= gap;
    break;
  default:
    shape.width -= gap;
    break;
  }
  return shape;
}

wxRect GlyphTabPainter::GetShapeRect(wxWindow *wnd, const wxRect &rect,
                                     wxDirection inner) const {
  wxRect shape = rect;
  if (m_style == Style::Pills) {
    // There is a gap around the pill.
    if (IsHorizontal(inner)) {
      shape.Deflate(wnd->FromDIP(kPillGapX), wnd->FromDIP(kPillGapY));
    } else {
      shape.Deflate(wnd->FromDIP(kVPillGapX), wnd->FromDIP(kVPillGapY));
    }
    return shape;
  }

  // Browser: the tab is a little away from the outer edge of the bar, and
  // touches the terminal on the inner edge.
  return WithoutOuterGap(wnd, rect, inner);
}

void GlyphTabPainter::DrawShape(wxDC &dc, wxWindow *wnd,
                                const GlyphTabInfo &tab, const wxRect &shape,
                                wxDirection inner) const {
  if (m_style == Style::Pills) {
    if (tab.active || tab.hover) {
      // The current pill has a thin outline in the accent colour.
      DrawRoundedRect(dc, shape, wnd->FromDIP(kPillRadius), kAllCorners,
                      tab.active ? m_bgActive : m_bgHover,
                      tab.active ? m_outline : wxColour{},
                      std::max(1, wnd->FromDIP(1)));
    }
    return;
  }

  if (tab.active || tab.hover) {
    // Rounded corners on the outer side only. The inner side touches the
    // terminal, so the tab looks like a part of it, also when it is hovered.
    int corners = kAllCorners;
    switch (inner) {
    case wxBOTTOM:
      corners = kTopLeft | kTopRight;
      break;
    case wxTOP:
      corners = kBottomLeft | kBottomRight;
      break;
    case wxRIGHT:
      corners = kTopLeft | kBottomLeft;
      break;
    default:
      corners = kTopRight | kBottomRight;
      break;
    }
    const int radius = wnd->FromDIP(kTabRadius);
    const int width = std::max(1, wnd->FromDIP(1));
    if (!tab.active) {
      // The hovered tab stops above the line along the side next to the pages,
      // so the line is still visible under it.
      wxRect hover = shape;
      switch (inner) {
      case wxBOTTOM:
        hover.height -= width;
        break;
      case wxTOP:
        hover.y += width;
        hover.height -= width;
        break;
      case wxRIGHT:
        hover.width -= width;
        break;
      default:
        hover.x += width;
        hover.width -= width;
        break;
      }
      DrawRoundedRect(dc, hover, radius, corners, m_bgHover);
      return;
    }
    // The current tab has the outline colour on its outer sides, so it stands
    // out from the bar (the colours of the bar and the tab can be close).
    DrawRoundedRect(dc, shape, radius, corners, m_bgActive, m_outline, width);
    // The side next to the terminal has no outline: paint it with the tab
    // colour, so the tab joins the terminal.
    // The strip stops one line-width before each end of the side, so the two
    // outline sides keep their colour down to the line along the tab.
    wxRect strip = InnerStrip(shape, inner, width);
    if (IsHorizontal(inner)) {
      strip.x += width;
      strip.width -= 2 * width;
    } else {
      strip.y += width;
      strip.height -= 2 * width;
    }
    dc.SetBrush(m_bgActive);
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.DrawRectangle(strip);
  }
}

void GlyphTabPainter::DrawContent(wxDC &dc, wxWindow *wnd, GlyphTabInfo &tab,
                                  const wxRect &shape) {
  // The icon, if any.
  int xStart = shape.x + wnd->FromDIP(kPaddingX);
  if (tab.bitmap.IsOk()) {
    const wxBitmap bmp = tab.bitmap.GetBitmapFor(wnd);
    const wxSize bitmapSize = bmp.GetLogicalSize();
    dc.DrawBitmap(bmp, xStart, shape.y + (shape.height - bitmapSize.y) / 2,
                  true);
    xStart += bitmapSize.x + wnd->FromDIP(kMargin);
  }

  const int xEnd = DrawButtons(dc, wnd, tab, shape);

  // The text.
  // The current tab uses the normal font too: AUI makes the selected font
  // bold, and the bold measuring font would make every tab wider.
  dc.SetFont(m_font);
  dc.SetTextForeground(tab.active  ? m_fgActive
                       : tab.hover ? m_fgHover
                                   : m_fgNormal);
  // The width of the tab comes from GetTextExtent(). Ellipsize() adds up the
  // width of each character, which on Windows is a few pixels more, so it would
  // cut a label that has all the room it measured. Use it only when the label
  // really does not fit.
  const int textRoom = std::max(xEnd - xStart, 0);
  wxString text = tab.caption;
  if (dc.GetTextExtent(text).x > textRoom) {
    text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, textRoom);
  }
  // The height of the font, not of the text, so labels with and without
  // emoji are on the same line.
  const int textHeight = dc.GetCharHeight();
  dc.DrawText(text, xStart, shape.y + (shape.height - textHeight) / 2);
}

int GlyphTabPainter::GetButtonsWidth(wxWindow *wnd,
                                     const GlyphTabInfo &tab) const {
  int width = 0;
  for (const auto &button : tab.buttons) {
    width += ButtonSize(wnd, button).x;
  }
  return width;
}

wxSize GlyphTabPainter::ButtonSize(wxWindow *wnd,
                                   const GlyphTabButton &button) const {
  if (button.close) {
    return wxSize(wnd->FromDIP(kCloseSize), wnd->FromDIP(kCloseSize));
  }
  return button.bitmap.IsOk() ? button.bitmap.GetBitmapFor(wnd).GetLogicalSize()
                              : wxSize();
}

int GlyphTabPainter::DrawButtons(wxDC &dc, wxWindow *wnd, GlyphTabInfo &tab,
                                 const wxRect &shape) {
  // The buttons, at the right end. Their room is always kept, so the width of
  // a tab does not change when the mouse moves over it, but they are drawn only
  // on the current tab and on the tab under the mouse.
  const int buttonsWidth = GetButtonsWidth(wnd, tab);
  int xEnd = shape.x + shape.width -
             wnd->FromDIP(buttonsWidth ? kButtonPaddingX : kPaddingX);
  if (!buttonsWidth) {
    return xEnd;
  }

  const bool showButtons = tab.active || tab.hover;
  xEnd -= buttonsWidth;
  int buttonX = xEnd;
  for (auto &button : tab.buttons) {
    const wxSize buttonSize = ButtonSize(wnd, button);
    if (!showButtons) {
      // An empty rectangle cannot be clicked.
      button.rect = wxRect();
      buttonX += buttonSize.x;
      continue;
    }
    button.rect = wxRect(buttonX, shape.y + (shape.height - buttonSize.y) / 2,
                         buttonSize.x, buttonSize.y);
    // The state is a bit mask: a pressed button is also hovered.
    const bool pressed = (button.state & GlyphTabButton::kPressed) != 0;
    if (pressed) {
      // Move a pressed button a little, so it looks pressed.
      button.rect.Offset(1, 1);
    }
    if (button.close) {
      // The X is dimmed; while the mouse is over it, it is in the text
      // colour, inside a rounded square. The square is darker while the
      // button is pressed.
      const bool hot = pressed || (button.state & GlyphTabButton::kHover) != 0;
      const wxColour &tabBg = tab.active ? m_bgActive : m_bgHover;
      // The text colour of the tab, for the X on it.
      const wxColour &tabFg = tab.active ? m_fgActive : m_fgHover;
      wxColour highlight;
      if (hot) {
        highlight = Mix(tabBg, tabFg, 0.25);
        if (pressed) {
          highlight = Mix(highlight, *wxBLACK, 0.25);
        }
      }
      DrawCloseButton(
          dc, button.rect, highlight, wnd->FromDIP(kCloseHighlightGrow),
          wnd->FromDIP(kCloseHighlightRadius), hot ? tabFg : m_fgDimmed);
    } else if (button.bitmap.IsOk()) {
      dc.DrawBitmap(button.bitmap.GetBitmapFor(wnd), button.rect.GetPosition(),
                    true);
    }
    buttonX += buttonSize.x;
  }
  return xEnd - wnd->FromDIP(kButtonGap);
}

wxSize GlyphTabPainter::GetContentSize(wxWindow *wnd,
                                       const GlyphTabInfo &tab) const {
  // The window measures the text, so this works also while a paint event runs
  // (a wxClientDC cannot be used then on some platforms). The height of the
  // font is the height of a line, so labels with and without emoji have the
  // same height.
  wxSize size;
  wnd->GetTextExtent(tab.caption, &size.x, &size.y, nullptr, nullptr, &m_font);
  int lineHeight = 0;
  wnd->GetTextExtent(wxS("Xg"), nullptr, &lineHeight, nullptr, nullptr,
                     &m_font);
  size.y = std::max(size.y, lineHeight);

  if (const int buttonsWidth = GetButtonsWidth(wnd, tab)) {
    size.x += wnd->FromDIP(kButtonGap) + buttonsWidth;
  }

  if (tab.bitmap.IsOk()) {
    const wxSize bitmapSize = tab.bitmap.GetPreferredLogicalSizeFor(wnd);
    size.x += bitmapSize.x + wnd->FromDIP(kMargin);
    size.IncTo(bitmapSize);
  }
  return size;
}

wxSize GlyphTabPainter::GetTabSize(wxWindow *wnd, const GlyphTabInfo &tab) {
  wxSize size = GetContentSize(wnd, tab);

  // Each padding converted on its own, not the conversion of the sum: the
  // drawing uses each padding once per side, and the two can differ by a pixel
  // with a scale like 105% or 150%. With buttons, the right one is smaller.
  size.x +=
      wnd->FromDIP(kPaddingX) +
      wnd->FromDIP(GetButtonsWidth(wnd, tab) ? kButtonPaddingX : kPaddingX);
  size.y += 2 * wnd->FromDIP(kPaddingY);
  if (m_style == Style::Pills) {
    size.x += 2 * wnd->FromDIP(kPillGapX);
    size.y += 2 * wnd->FromDIP(kPillGapY);
  } else {
    size.y += wnd->FromDIP(kTopGap);
  }
  return size;
}

int GlyphTabPainter::GetVerticalTabHeight(wxWindow *wnd,
                                          const GlyphTabInfo &tab) {
  int height = GetContentSize(wnd, tab).y + 2 * wnd->FromDIP(kPaddingY);
  if (m_style == Style::Pills) {
    height += 2 * wnd->FromDIP(kVPillGapY);
  }
  return height;
}

namespace {
// The header of a group is measured like a tab with the name as its label.
GlyphTabInfo HeaderTab(const GlyphGroupInfo &group) {
  GlyphTabInfo tab;
  tab.caption = group.name;
  return tab;
}
} // namespace

wxSize GlyphTabPainter::GetGroupHeaderSize(wxWindow *wnd,
                                           const GlyphGroupInfo &group) {
  wxSize size = GetTabSize(wnd, HeaderTab(group));
  // The triangle, and the space after it when there is a name. The chip is as
  // high as the tab.
  size.x += IndicatorSize(wnd, size.y) +
            (group.name.empty() ? 0 : wnd->FromDIP(kIndicatorGap));
  size.x = std::max(size.x, wnd->FromDIP(kMinHeaderWidth));
  return size;
}

int GlyphTabPainter::GetVerticalGroupHeaderHeight(wxWindow *wnd,
                                                  const GlyphGroupInfo &group) {
  return GetVerticalTabHeight(wnd, HeaderTab(group));
}

int GlyphTabPainter::IndicatorSize(wxWindow *wnd, int chipHeight) const {
  return std::max(wnd->FromDIP(kMinIndicatorSize), chipHeight / 2);
}

wxBitmap GlyphTabPainter::GetIndicatorBitmap(bool collapsed,
                                             const wxColour &colour, int size) {
  const auto key = std::make_tuple(collapsed, colour.GetRGB(), size);
  if (const auto found = m_indicatorCache.find(key);
      found != m_indicatorCache.end()) {
    return found->second;
  }
  // The SVG is made here, in memory: no file is needed. The triangle has the
  // colour of the text of the chip. One that points to the right is a collapsed
  // group, one that points down is a group that is shown.
  const wxString path =
      collapsed ? wxS("M4 2 L14 8 L4 14 Z") : wxS("M2 4 L14 4 L8 14 Z");
  const wxString svg =
      wxString::Format(wxS("<svg xmlns=\"http://www.w3.org/2000/svg\" "
                           "width=\"16\" height=\"16\" "
                           "viewBox=\"0 0 16 16\"><path d=\"%s\" "
                           "fill=\"#%02x%02x%02x\"/></svg>"),
                       path, colour.Red(), colour.Green(), colour.Blue());
  const wxScopedCharBuffer data = svg.utf8_str();
  const wxBitmapBundle bundle =
      wxBitmapBundle::FromSVG(data.data(), wxSize(size, size));
  wxBitmap bitmap =
      bundle.IsOk() ? bundle.GetBitmap(wxSize(size, size)) : wxBitmap();
  // Few sizes and colours are used, but do not let the cache grow without end.
  if (m_indicatorCache.size() > 64) {
    m_indicatorCache.clear();
  }
  m_indicatorCache[key] = bitmap;
  return bitmap;
}

// A triangle in @p rect: it points to the right for a collapsed group, and down
// for a group that is shown. Used when the SVG one cannot be made. It is
// anti-aliased when a graphics context is available.
static void DrawGroupIndicator(wxDC &dc, const wxRect &rect, bool collapsed,
                               const wxColour &colour) {
  // The corners of the triangle as fractions of the square.
  static const double kRight[3][2] = {{0.25, 0.1}, {0.25, 0.9}, {0.85, 0.5}};
  static const double kDown[3][2] = {{0.1, 0.25}, {0.9, 0.25}, {0.5, 0.85}};
  const double (*corners)[2] = collapsed ? kRight : kDown;

  std::unique_ptr<wxGraphicsContext> gc{
      wxGraphicsContext::CreateFromUnknownDC(dc)};
  if (gc) {
    gc->SetBrush(wxBrush(colour));
    gc->SetPen(*wxTRANSPARENT_PEN);
    wxGraphicsPath path = gc->CreatePath();
    path.MoveToPoint(rect.x + rect.width * corners[0][0],
                     rect.y + rect.height * corners[0][1]);
    for (int k = 1; k < 3; ++k) {
      path.AddLineToPoint(rect.x + rect.width * corners[k][0],
                          rect.y + rect.height * corners[k][1]);
    }
    path.CloseSubpath();
    gc->FillPath(path);
    return;
  }
  wxPoint points[3];
  for (int k = 0; k < 3; ++k) {
    points[k] = wxPoint(rect.x + static_cast<int>(rect.width * corners[k][0]),
                        rect.y + static_cast<int>(rect.height * corners[k][1]));
  }
  dc.SetBrush(colour);
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.DrawPolygon(3, points);
}

void GlyphTabPainter::DrawGroupHeader(wxDC &dc, wxWindow *wnd,
                                      GlyphGroupInfo &group, const wxRect &rect,
                                      wxDirection inner) {
  wxDCClipper clip(dc, rect);
  group.rect = rect;
  const wxColour colour =
      group.colour.IsOk() ? group.colour : DefaultGroupColour();

  // The same in both styles: a plain rectangle with no rounded corner, and as
  // big as the whole row, with no margin.
  const wxRect chip = rect;
  const int corners = 0;
  const wxColour textColour = TextOn(colour, 0.92);
  const wxColour fill =
      group.hover ? Mix(colour, TextOn(colour, 1.0), 0.15) : colour;
  DrawRoundedRect(dc, chip, wnd->FromDIP(kTabRadius), corners, fill);

  dc.SetFont(m_font);
  dc.SetTextForeground(textColour);
  const int padding = wnd->FromDIP(kPaddingX);
  const int indicator = IndicatorSize(wnd, chip.height);
  const int gap = group.name.empty() ? 0 : wnd->FromDIP(kIndicatorGap);
  wxString text = group.name;
  const int room = std::max(chip.width - 2 * padding - indicator - gap, 0);
  if (dc.GetTextExtent(text).x > room) {
    text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, room);
  }
  const int textWidth = text.empty() ? 0 : dc.GetTextExtent(text).x;
  // The triangle and the name together: centred in a horizontal bar, from the
  // start in a vertical one.
  const int content = indicator + gap + textWidth;
  const int x = IsHorizontal(inner) ? chip.x + (chip.width - content) / 2
                                    : chip.x + padding;
  const wxRect indicatorRect(x, chip.y + (chip.height - indicator) / 2,
                             indicator, indicator);
  const wxBitmap triangle =
      GetIndicatorBitmap(group.collapsed, textColour, indicator);
  if (triangle.IsOk()) {
    dc.DrawBitmap(triangle, indicatorRect.GetPosition(), true);
  } else {
    // No SVG support: draw the triangle.
    DrawGroupIndicator(dc, indicatorRect, group.collapsed, textColour);
  }
  if (!text.empty()) {
    dc.DrawText(text, x + indicator + gap,
                chip.y + (chip.height - dc.GetCharHeight()) / 2);
  }
}

void GlyphTabPainter::DrawGroupLine(wxDC &dc, wxWindow *wnd, const wxRect &span,
                                    const wxColour &colour, wxDirection inner) {
  const wxColour lineColour = colour.IsOk() ? colour : DefaultGroupColour();
  // The line is along the side of the bar that is next to the pages: at the
  // bottom of a bar above the pages, at the top of a bar below them, on the
  // right of a bar on the left, and on the left of a bar on the right.
  const int width = std::max(2, wnd->FromDIP(kGroupLineWidth));
  wxRect line;
  switch (inner) {
  case wxBOTTOM:
    line = wxRect(span.x, span.GetBottom() + 1 - width, span.width, width);
    break;
  case wxTOP:
    line = wxRect(span.x, span.y, span.width, width);
    break;
  case wxRIGHT:
    line = wxRect(span.GetRight() + 1 - width, span.y, width, span.height);
    break;
  default:
    line = wxRect(span.x, span.y, width, span.height);
    break;
  }
  dc.SetBrush(lineColour);
  dc.SetPen(*wxTRANSPARENT_PEN);
  dc.DrawRectangle(line);
}
