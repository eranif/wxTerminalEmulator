#include "theme_loader.h"

#include "terminal_logger.h"

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/textfile.h>

#include <algorithm>

namespace {

// Returns the value of a TOML "key = value" line without quotes and without a
// trailing "# comment".
wxString ParseValue(wxString s) {
  s.Trim(true).Trim(false);
  if (!s.empty() && (s[0] == '\'' || s[0] == '"')) {
    const wxUniChar quote = s[0];
    const size_t close = s.find(quote, 1);
    return close == wxString::npos ? s.Mid(1) : s.Mid(1, close - 1);
  }
  const int hash = s.Find('#');
  if (hash != wxNOT_FOUND) {
    s = s.Left(hash);
  }
  return s.Trim(true);
}

struct ColourKey {
  const char *table;
  const char *key;
  wxColour wxTerminalTheme::*member;
};

// clang-format off
const ColourKey kColourKeys[] = {
    {"colors.primary",   "background", &wxTerminalTheme::bg},
    {"colors.primary",   "foreground", &wxTerminalTheme::fg},
    {"colors.cursor",    "cursor",     &wxTerminalTheme::cursorColour},
    {"colors.selection", "background", &wxTerminalTheme::selectionBg},
    {"colors.selection", "text",       &wxTerminalTheme::selectionFg},
    {"colors.normal",    "black",      &wxTerminalTheme::black},
    {"colors.normal",    "red",        &wxTerminalTheme::red},
    {"colors.normal",    "green",      &wxTerminalTheme::green},
    {"colors.normal",    "yellow",     &wxTerminalTheme::yellow},
    {"colors.normal",    "blue",       &wxTerminalTheme::blue},
    {"colors.normal",    "magenta",    &wxTerminalTheme::magenta},
    {"colors.normal",    "cyan",       &wxTerminalTheme::cyan},
    {"colors.normal",    "white",      &wxTerminalTheme::white},
    {"colors.bright",    "black",      &wxTerminalTheme::brightBlack},
    {"colors.bright",    "red",        &wxTerminalTheme::brightRed},
    {"colors.bright",    "green",      &wxTerminalTheme::brightGreen},
    {"colors.bright",    "yellow",     &wxTerminalTheme::brightYellow},
    {"colors.bright",    "blue",       &wxTerminalTheme::brightBlue},
    {"colors.bright",    "magenta",    &wxTerminalTheme::brightMagenta},
    {"colors.bright",    "cyan",       &wxTerminalTheme::brightCyan},
    {"colors.bright",    "white",      &wxTerminalTheme::brightWhite},
};
// clang-format on

} // namespace

wxString ShippedThemesDir() {
#ifdef __WXMAC__
  wxFileName dir(wxStandardPaths::Get().GetResourcesDir(), "");
#else
  wxFileName dir(wxStandardPaths::Get().GetExecutablePath());
  dir.SetFullName("");
#endif
  dir.AppendDir("themes");
  return dir.GetPath();
}

bool ParseThemeTomlFile(const wxString &path, wxTerminalTheme *out) {
  wxTextFile file;
  if (!file.Open(path)) {
    TLOG_WARN() << "Theme: could not open " << path << std::endl;
    return false;
  }

  // Start from the existing values so unspecified fields stay sensible.
  wxTerminalTheme theme = *out;
  wxString table;
  int assigned = 0;
  bool hasCursor = false;

  for (size_t i = 0; i < file.GetLineCount(); ++i) {
    wxString line = file.GetLine(i);
    line.Trim(true).Trim(false);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (line[0] == '[') {
      const int close = line.Find(']');
      if (close != wxNOT_FOUND) {
        table = line.Mid(1, close - 1).Trim(true).Trim(false);
      }
      continue;
    }
    const int eq = line.Find('=');
    if (eq == wxNOT_FOUND) {
      continue;
    }
    const wxString key = line.Left(eq).Trim(true).Trim(false);
    // Values such as 'CellForeground' are not colours and are skipped.
    const wxColour c(ParseValue(line.Mid(eq + 1)));
    if (!c.IsOk()) {
      continue;
    }
    for (const auto &entry : kColourKeys) {
      if (table == entry.table && key == entry.key) {
        theme.*(entry.member) = c;
        hasCursor = hasCursor || entry.member == &wxTerminalTheme::cursorColour;
        ++assigned;
        break;
      }
    }
  }

  if (assigned == 0) {
    TLOG_WARN() << "Theme: no usable colours parsed from " << path << std::endl;
    return false;
  }
  // Most files have no cursor colour. The default (white) is not visible on a
  // light background, so use the text colour.
  if (!hasCursor) {
    theme.cursorColour = theme.fg;
  }
  *out = theme;
  return true;
}

std::vector<LoadedTheme> LoadShippedThemes() {
  std::vector<LoadedTheme> themes;

  const wxString path = ShippedThemesDir();
  wxDir dir;
  if (!wxDir::Exists(path) || !dir.Open(path)) {
    TLOG_INFO() << "No shipped themes directory: " << path << std::endl;
    return themes;
  }

  wxString name;
  for (bool more = dir.GetFirst(&name, "*.toml", wxDIR_FILES); more;
       more = dir.GetNext(&name)) {
    const wxFileName fn(path, name);
    LoadedTheme lt;
    lt.name = fn.GetName();
    if (ParseThemeTomlFile(fn.GetFullPath(), &lt.theme)) {
      themes.push_back(std::move(lt));
    }
  }

  std::sort(themes.begin(), themes.end(),
            [](const LoadedTheme &a, const LoadedTheme &b) {
              return a.name.CmpNoCase(b.name) < 0;
            });
  TLOG_DEBUG() << "Loaded " << themes.size() << " shipped theme(s) from "
               << path << std::endl;
  return themes;
}
