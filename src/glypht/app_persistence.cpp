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

const std::vector<TabOutlineColour> &AppConfig::GetTabOutlineColours() {
  // clang-format off
  static const std::vector<TabOutlineColour> colours = {
      {"fg",            "Text",           &wxTerminalTheme::fg},
      {"red",           "Red",            &wxTerminalTheme::red},
      {"green",         "Green",          &wxTerminalTheme::green},
      {"yellow",        "Yellow",         &wxTerminalTheme::yellow},
      {"blue",          "Blue",           &wxTerminalTheme::blue},
      {"magenta",       "Magenta",        &wxTerminalTheme::magenta},
      {"cyan",          "Cyan",           &wxTerminalTheme::cyan},
      {"white",         "White",          &wxTerminalTheme::white},
      {"brightred",     "Bright Red",     &wxTerminalTheme::brightRed},
      {"brightgreen",   "Bright Green",   &wxTerminalTheme::brightGreen},
      {"brightyellow",  "Bright Yellow",  &wxTerminalTheme::brightYellow},
      {"brightblue",    "Bright Blue",    &wxTerminalTheme::brightBlue},
      {"brightmagenta", "Bright Magenta", &wxTerminalTheme::brightMagenta},
      {"brightcyan",    "Bright Cyan",    &wxTerminalTheme::brightCyan},
      {"brightwhite",   "Bright White",   &wxTerminalTheme::brightWhite},
      {"selection",     "Selection",      &wxTerminalTheme::selectionBg},
      {"highlight",     "Search Match",   &wxTerminalTheme::highlightBg},
      {"cursor",        "Cursor",         &wxTerminalTheme::cursorColour},
      {"link",          "Link",           &wxTerminalTheme::linkColour},
  };
  // clang-format on
  return colours;
}

wxColour AppConfig::GetTabOutlineColourOf(const wxTerminalTheme &theme,
                                          const wxString &key) {
  wxColour colour = theme.magenta;
  for (const auto &entry : GetTabOutlineColours()) {
    if (key == entry.key) {
      colour = theme.*(entry.member);
      break;
    }
  }
  // Some theme colours have alpha; the outline is solid.
  return wxColour(colour.Red(), colour.Green(), colour.Blue());
}

wxString AppConfig::TabPositionToString(TabPosition position) {
  switch (position) {
  case TabPosition::Bottom:
    return "bottom";
  case TabPosition::Left:
    return "left";
  case TabPosition::Right:
    return "right";
  default:
    return "top";
  }
}

TabPosition AppConfig::TabPositionFromString(const wxString &value) {
  const wxString v = value.Lower();
  if (v == "bottom") {
    return TabPosition::Bottom;
  }
  if (v == "left") {
    return TabPosition::Left;
  }
  if (v == "right") {
    return TabPosition::Right;
  }
  return TabPosition::Top;
}

wxString AppConfig::TabStyleToString(TabStyle style) {
  return style == TabStyle::Pills ? "pills" : "browser";
}

TabStyle AppConfig::TabStyleFromString(const wxString &value) {
  return value.Lower() == "pills" ? TabStyle::Pills : TabStyle::Browser;
}

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
  {
    TerminalLogger::Get().SetLevel(TerminalLogger::Get().GetLevel());
  }

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
      } else if (key == "taboutline") {
        config.SetTabOutlineColour(value);
      } else if (key == "tabposition") {
        config.SetTabPosition(AppConfig::TabPositionFromString(value));
      } else if (key == "tabstyle") {
        config.SetTabStyle(AppConfig::TabStyleFromString(value));
      } else if (key == "tabbarwidth") {
        long width = 0;
        if (value.ToLong(&width) && width > 0) {
          config.SetTabBarWidth(static_cast<int>(width));
        }
      } else if (key == "shell") {
        config.SetShellExecutable(value);
      } else if (key == "lastshell") {
        config.SetLastShell(value);
      } else if (key == "shellargs") {
        config.SetShellArgs(value);
      } else if (key == "tabswitchersize") {
        wxArrayString parts = wxSplit(value, ',');
        if (parts.size() == 2) {
          long w = 520, h = 260;
          parts[0].ToLong(&w);
          parts[1].ToLong(&h);
          config.SetTabSwitcherSize(
              wxSize(static_cast<int>(w), static_cast<int>(h)));
        }
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
  file.AddLine("taboutline=" + config.GetTabOutlineColour());
  file.AddLine("tabposition=" +
               AppConfig::TabPositionToString(config.GetTabPosition()));
  file.AddLine("tabstyle=" + AppConfig::TabStyleToString(config.GetTabStyle()));
  file.AddLine(wxString::Format("tabbarwidth=%d", config.GetTabBarWidth()));
  file.AddLine("font=" + config.GetFont().GetNativeFontInfoDesc());
  file.AddLine(wxString::Format(
      "safedrawing=%s", config.IsSafeDrawingEnabled() ? "true" : "false"));
  file.AddLine("shell=" + config.GetShellExecutable());
  file.AddLine("shellargs=" + config.GetShellArgs());
  file.AddLine("lastshell=" + config.GetLastShell());
  wxSize tabSwitcherSize = config.GetTabSwitcherSize();
  file.AddLine(wxString::Format("tabswitchersize=%d,%d",
                                tabSwitcherSize.GetWidth(),
                                tabSwitcherSize.GetHeight()));

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
