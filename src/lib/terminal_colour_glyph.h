#pragma once

// Helpers to draw emoji with colour. GDI draws every glyph in one colour, so on
// Windows an emoji is drawn with Direct2D, which draws the colour glyph of the
// emoji font. Shared by the OpenGL renderer (which draws a glyph into an atlas
// tile) and the wxDC renderer (which draws it into the backing store).

#include <algorithm>
#include <cmath>
#include <memory>
#include <wx/colour.h>
#include <wx/dcmemory.h>
#include <wx/font.h>
#include <wx/gdicmn.h>
#include <wx/string.h>
#ifdef __WXMSW__
#include <wx/graphics.h>
#endif

namespace terminal {

/// True for emoji and symbol glyphs: they are drawn with the colour path and
/// fitted to their cell. This leaves out the CJK punctuation and circled
/// ideographs: they have no colour.
inline bool IsEmojiGlyph(char32_t ch) {
  return ch > 0xFFFF || (ch >= 0x2300 && ch <= 0x23FF) ||
         (ch >= 0x2600 && ch <= 0x27BF) || (ch >= 0x2B00 && ch <= 0x2BFF);
}

/// The font with its size multiplied by `factor`.
inline wxFont ScaleFont(wxFont font, double factor) {
  const double pt = font.GetFractionalPointSize();
  if (pt > 0 && factor > 0) {
    font.SetFractionalPointSize(pt * factor);
  }
  return font;
}

#ifdef __WXMSW__
/// Draw `text` with Direct2D into `box` of `dc`, scaled to fit the box and
/// centred in it. A colour glyph keeps its own colours; a one-colour glyph
/// uses `fg`. Returns false if Direct2D is not available, so the caller draws
/// the glyph with GDI instead.
///
/// The colour needs a wxWidgets whose Direct2D renderer draws colour fonts. It
/// was checked with the wxWidgets version of build.sh (WX_VERSION, v3.3.4).
/// With a renderer that does not, the glyph is drawn in one colour.
inline bool DrawColourGlyphD2D(wxMemoryDC &dc, const wxString &text,
                               wxFont font, const wxColour &fg,
                               const wxRect &box) {
#if wxUSE_GRAPHICS_DIRECT2D
  wxGraphicsRenderer *renderer = wxGraphicsRenderer::GetDirect2DRenderer();
  if (!renderer) {
    return false;
  }
  std::unique_ptr<wxGraphicsContext> gc{renderer->CreateContext(dc)};
  if (!gc) {
    return false;
  }
  wxDouble w = 0, h = 0;
  gc->SetFont(font, fg);
  gc->GetTextExtent(text, &w, &h);
  // Direct2D can convert points to pixels differently from GDI (for example on
  // a HiDPI display), so make the line height match the box first.
  if (h > 0 && std::abs(h - box.height) > 1) {
    font = ScaleFont(font, box.height / h);
    gc->SetFont(font, fg);
    gc->GetTextExtent(text, &w, &h);
  }
  if (w > box.width) {
    font = ScaleFont(font, box.width / w);
    gc->SetFont(font, fg);
    gc->GetTextExtent(text, &w, &h);
  }
  gc->DrawText(text, box.x + (box.width - w) / 2, box.y + (box.height - h) / 2);
  return true;
#else
  wxUnusedVar(dc);
  wxUnusedVar(text);
  wxUnusedVar(font);
  wxUnusedVar(fg);
  wxUnusedVar(box);
  return false;
#endif
}
#endif // __WXMSW__

} // namespace terminal
