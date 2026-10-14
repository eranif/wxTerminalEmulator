#include "app_persistence.h"

#include "terminal_logger.h"
#include "theme_loader.h"
#include <algorithm>
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

bool SameColours(const wxTerminalTheme &a, const wxTerminalTheme &b) {
  for (const auto &entry : kColourEntries) {
    if (a.*(entry.member) != b.*(entry.member)) {
      return false;
    }
  }
  return true;
}

} // namespace

AppConfig::AppConfig() {
  themes["dark"] = wxTerminalTheme::MakeDarkTheme();
  themes["light"] = wxTerminalTheme::MakeLightTheme();
  for (auto &loaded : LoadShippedThemes()) {
    if (themes.count(loaded.name) == 0) {
      shippedThemes[loaded.name] = std::move(loaded.theme);
    }
  }
}

const wxTerminalTheme *AppConfig::FindTheme(const wxString &name) const {
  auto it = themes.find(name);
  if (it != themes.end()) {
    return &it->second;
  }
  it = shippedThemes.find(name);
  if (it != shippedThemes.end()) {
    return &it->second;
  }
  return nullptr;
}

wxTerminalTheme AppConfig::GetThemeByName(const wxString &name) const {
  const wxTerminalTheme *theme = FindTheme(name);
  return theme ? *theme : MakeBaseTheme(name);
}

const wxTerminalTheme &AppConfig::GetActiveTheme() const {
  const wxTerminalTheme *theme = FindTheme(GetThemeName());
  if (theme) {
    return *theme;
  }
  static const wxTerminalTheme fallback = wxTerminalTheme::MakeDarkTheme();
  return fallback;
}

void AppConfig::SetTheme(const wxString &name, const wxTerminalTheme &theme) {
  auto shipped = shippedThemes.find(name);
  if (shipped != shippedThemes.end() && SameColours(theme, shipped->second)) {
    themes.erase(name);
    return;
  }
  themes[name] = theme;
}

void AppConfig::RemoveTheme(const wxString &name) {
  if (name == "dark" || name == "light") {
    return;
  }
  themes.erase(name);
}

std::vector<wxString> AppConfig::GetThemeNames() const {
  std::vector<wxString> names = GetUserThemeNames();
  for (const auto &pair : shippedThemes) {
    if (themes.count(pair.first) == 0) {
      names.push_back(pair.first);
    }
  }
  std::sort(
      names.begin(), names.end(),
      [](const wxString &a, const wxString &b) { return a.CmpNoCase(b) < 0; });
  return names;
}

std::vector<wxString> AppConfig::GetUserThemeNames() const {
  std::vector<wxString> names;
  names.reserve(themes.size());
  for (const auto &pair : themes) {
    names.push_back(pair.first);
  }
  return names;
}

bool AppConfig::HasTheme(const wxString &name) const {
  return FindTheme(name) != nullptr;
}

wxString AppConfig::GetShellCommand() const {
  wxString exe = shellExecutable;
  exe.Trim(true).Trim(false);
  if (exe.empty()) {
    return {};
  }
  // An absolute path must point to a file. A plain name such as "pwsh" is left
  // to the operating system, which looks it up in PATH.
  const wxFileName fn(exe);
  if (fn.IsAbsolute() && !fn.FileExists()) {
    return {};
  }
  wxString command = exe.Contains(' ') ? "\"" + exe + "\"" : exe;
  wxString args = shellArgs;
  args.Trim(true).Trim(false);
  if (!args.empty()) {
    command << ' ' << args;
  }
  return command;
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
      } else if (key == "newtabtitle") {
        config.SetNewTabTitle(value);
      } else if (key == "showclosebutton") {
        config.SetShowCloseButton(value == "1" || value == "true" ||
                                  value == "yes" || value == "on");
      } else if (key == "blockcursor") {
        config.SetBlockCursor(value == "1" || value == "true" ||
                              value == "yes" || value == "on");
      } else if (key == "shell") {
        config.SetShellExecutable(value);
      } else if (key == "shellargs") {
        config.SetShellArgs(value);
      }
    } else if (section == Section::kTheme) {
      if (key == "name") {
        currentThemeName = value;
        // Start from the shipped theme of the same name, if there is one, so
        // colours that are not saved (cursor, selection) keep its values.
        currentTheme = config.GetThemeByName(value);
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
  file.AddLine("newtabtitle=" + config.GetNewTabTitle());
  file.AddLine(wxString::Format(
      "showclosebutton=%s", config.GetShowCloseButton() ? "true" : "false"));
  file.AddLine(wxString::Format("blockcursor=%s",
                                config.GetBlockCursor() ? "true" : "false"));
  file.AddLine("font=" + config.GetFont().GetNativeFontInfoDesc());
  file.AddLine(wxString::Format(
      "safedrawing=%s", config.IsSafeDrawingEnabled() ? "true" : "false"));
  file.AddLine("shell=" + config.GetShellExecutable());
  file.AddLine("shellargs=" + config.GetShellArgs());

  // Shipped themes are read from their *.toml files, so only themes the user
  // created or edited are saved.
  for (const wxString &name : config.GetUserThemeNames()) {
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
