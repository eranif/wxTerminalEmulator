#pragma once

#include <wx/string.h>

// The directory that holds the fonts shipped with GlyphT:
//   - macOS: <bundle>/Contents/Resources/Fonts
//   - Windows and Linux: <executable dir>/fonts
wxString ShippedFontsDir();

// Makes the fonts shipped with GlyphT (Iosevka) available to this process, and
// makes Iosevka the default font of the terminal themes. Call it once at
// startup, before any theme or window is made. It does nothing when the
// wxWidgets port cannot load private fonts, or when the font is not there: the
// font of the platform is used then.
void LoadBundledFonts();
