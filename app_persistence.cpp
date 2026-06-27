#include "app_persistence.h"

#include "terminal_logger.h"
#include <wx/filename.h>
#include <wx/textfile.h>
#include <wx/utils.h>

namespace {

wxString ColourToHex(const wxColour &c) {
  return wxString::Format("#%02X%02X%02X", c.Red(), c.Green(), c.Blue());
}

wxColour HexToColour(const wxString &hex) {
  wxColour c;
  c.Set(hex);
  return c;
}

struct ColourEntry {
  const char *suffix;
  wxColour wxTerminalTheme::*member;
};

// clang-format off
static const ColourEntry kColourEntries[] = {
    {"fg",           &wxTerminalTheme::fg},
    {"bg",           &wxTerminalTheme::bg},
    {"black",        &wxTerminalTheme::black},
    {"red",          &wxTerminalTheme::red},
    {"green",        &wxTerminalTheme::green},
    {"yellow",       &wxTerminalTheme::yellow},
    {"blue",         &wxTerminalTheme::blue},
    {"magenta",      &wxTerminalTheme::magenta},
    {"cyan",         &wxTerminalTheme::cyan},
    {"white",        &wxTerminalTheme::white},
    {"brightblack",  &wxTerminalTheme::brightBlack},
    {"brightred",    &wxTerminalTheme::brightRed},
    {"brightgreen",  &wxTerminalTheme::brightGreen},
    {"brightyellow", &wxTerminalTheme::brightYellow},
    {"brightblue",   &wxTerminalTheme::brightBlue},
    {"brightmagenta",&wxTerminalTheme::brightMagenta},
    {"brightcyan",   &wxTerminalTheme::brightCyan},
    {"brightwhite",  &wxTerminalTheme::brightWhite},
};
// clang-format on

wxTerminalTheme MakeBaseTheme(const wxString &name) {
  if (name == "light") {
    return wxTerminalTheme::MakeLightTheme();
  }
  return wxTerminalTheme::MakeDarkTheme();
}

} // namespace

AppConfig::AppConfig() {
  themes["dark"] = wxTerminalTheme::MakeDarkTheme();
  themes["light"] = wxTerminalTheme::MakeLightTheme();
}

wxTerminalTheme AppConfig::GetThemeByName(const wxString &name) const {
  auto it = themes.find(name);
  if (it != themes.end()) {
    return it->second;
  }
  return MakeBaseTheme(name);
}

const wxTerminalTheme &AppConfig::GetActiveTheme() const {
  auto it = themes.find(GetThemeName());
  if (it != themes.end()) {
    return it->second;
  }
  static const wxTerminalTheme fallback = wxTerminalTheme::MakeDarkTheme();
  return fallback;
}

void AppConfig::SetTheme(const wxString &name, const wxTerminalTheme &theme) {
  themes[name] = theme;
}

void AppConfig::RemoveTheme(const wxString &name) {
  if (name == "dark" || name == "light") {
    return;
  }
  themes.erase(name);
}

std::vector<wxString> AppConfig::GetThemeNames() const {
  std::vector<wxString> names;
  names.reserve(themes.size());
  for (const auto &pair : themes) {
    names.push_back(pair.first);
  }
  return names;
}

bool AppConfig::HasTheme(const wxString &name) const {
  return themes.find(name) != themes.end();
}

wxString AppPersistence::GetConfigPath() {
  wxFileName logFile(TerminalLogger::Get().GetLevel() <=
                             TerminalLogLevel::kTrace
                         ? wxString{}
                         : wxString{});
  wxUnusedVar(logFile);

  wxString path;
  { TerminalLogger::Get().SetLevel(TerminalLogger::Get().GetLevel()); }

#ifdef __WXMSW__
  wxString dir;
  dir << R"(C:\Users\)" << ::wxGetUserId() << wxFileName::GetPathSeparator()
      << ".wxterminal";
#else
  wxString dir =
      wxFileName::GetHomeDir() + wxFileName::GetPathSeparator() + ".wxterminal";
#endif
  return dir + wxFileName::GetPathSeparator() + "config.ini";
}

bool AppPersistence::Load(AppConfig &config) {
  const wxString path = GetConfigPath();
  if (!wxFileName::FileExists(path))
    return false;

  wxTextFile file;
  if (!file.Open(path))
    return false;

  enum class Section { kGeneral, kTheme };
  Section section = Section::kGeneral;
  wxString currentThemeName;
  wxTerminalTheme currentTheme;

  auto flushTheme = [&]() {
    if (!currentThemeName.empty()) {
      config.SetTheme(currentThemeName, currentTheme);
    }
  };

  for (size_t i = 0; i < file.GetLineCount(); ++i) {
    wxString s = file.GetLine(i);
    s = s.Strip(wxString::both);
    if (s.empty() || s.StartsWith("#") || s.StartsWith(";"))
      continue;

    if (s == "[theme]") {
      flushTheme();
      section = Section::kTheme;
      currentThemeName.clear();
      currentTheme = wxTerminalTheme::MakeDarkTheme();
      continue;
    }
    if (s.StartsWith("[")) {
      flushTheme();
      section = Section::kGeneral;
      currentThemeName.clear();
      continue;
    }

    const int eq = s.Find('=');
    if (eq == wxNOT_FOUND)
      continue;

    const wxString key = s.Left(eq).Strip(wxString::both).Lower();
    const wxString value = s.Mid(eq + 1).Strip(wxString::both);

    if (section == Section::kGeneral) {
      if (key == "theme") {
        config.SetThemeName(value);
      } else if (key == "font") {
        wxFont f;
        f.SetNativeFontInfo(value);
        config.SetFont(f);
      } else if (key == "safedrawing") {
        config.SetSafeDrawingEnabled(value == "1" || value == "true" ||
                                     value == "yes" || value == "on");
      }
    } else if (section == Section::kTheme) {
      if (key == "name") {
        currentThemeName = value;
        currentTheme = MakeBaseTheme(value);
      } else {
        for (const auto &entry : kColourEntries) {
          if (key == entry.suffix) {
            currentTheme.*(entry.member) = HexToColour(value);
            break;
          }
        }
      }
    }
  }
  flushTheme();
  return true;
}

bool AppPersistence::Save(const AppConfig &config) {
  const wxString path = GetConfigPath();
  wxFileName fn(path);
  if (!fn.DirExists())
    wxFileName::Mkdir(fn.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

  wxTextFile file;
  if (wxFileName::FileExists(path)) {
    if (!file.Open(path))
      return false;
    file.Clear();
  } else {
    if (!file.Create(path))
      return false;
  }

  file.AddLine("theme=" + config.GetThemeName());
  file.AddLine("font=" + config.GetFont().GetNativeFontInfoDesc());
  file.AddLine(wxString::Format(
      "safedrawing=%s", config.IsSafeDrawingEnabled() ? "true" : "false"));

  for (const wxString &name : config.GetThemeNames()) {
    file.AddLine("");
    file.AddLine("[theme]");
    file.AddLine("name=" + name);
    const wxTerminalTheme &theme = config.GetThemeByName(name);
    for (const auto &entry : kColourEntries) {
      file.AddLine(wxString(entry.suffix) + "=" +
                   ColourToHex(theme.*(entry.member)));
    }
  }

  return file.Write();
}
