#pragma once

#include "terminal_theme.h"

#include <wx/string.h>

#include <vector>

// A named terminal colour theme parsed from a shipped .toml file.
struct LoadedTheme {
  wxString name; // display name (file basename without extension)
  wxTerminalTheme theme;
};

// The directory that holds the *.toml themes shipped with GlyphT:
//   - macOS: <bundle>/Contents/Resources/themes
//   - Windows and Linux: <executable dir>/themes
wxString ShippedThemesDir();

// Parses every *.toml file in ShippedThemesDir() into a wxTerminalTheme.
// Returns them sorted by name. Files that fail to parse are skipped and logged.
std::vector<LoadedTheme> LoadShippedThemes();

// Parses a single Alacritty-style TOML colour file into *out. Reads only the
// [colors.primary], [colors.cursor], [colors.selection], [colors.normal] and
// [colors.bright] tables. Unknown keys are ignored. Returns false on a read
// failure or if no usable colour was found (then *out is not changed).
bool ParseThemeTomlFile(const wxString &path, wxTerminalTheme *out);
