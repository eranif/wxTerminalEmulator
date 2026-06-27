#pragma once

#include "terminal_theme.h"
#include <map>
#include <vector>
#include <wx/font.h>
#include <wx/string.h>

struct AppConfig {
  AppConfig();

  wxTerminalTheme GetThemeByName(const wxString &name) const;
  const wxTerminalTheme &GetActiveTheme() const;

  void SetThemeName(const wxString &themeName) { this->themeName = themeName; }
  wxString GetThemeName() const {
    if (themeName.empty()) {
      return "dark";
    }
    return themeName;
  }

  void SetTheme(const wxString &name, const wxTerminalTheme &theme);
  void RemoveTheme(const wxString &name);
  std::vector<wxString> GetThemeNames() const;
  bool HasTheme(const wxString &name) const;

  void SetFont(const wxFont &font) { this->font = font; }
  const wxFont &GetFont() const { return font; }

  void SetSafeDrawingEnabled(bool safeDrawingEnabled) {
#if USE_OPENGL
    wxUnusedVar(safeDrawingEnabled);
#else
    this->safeDrawingEnabled = safeDrawingEnabled;
#endif
  }
  bool IsSafeDrawingEnabled() const {
#if USE_OPENGL
    return false;
#endif
    return safeDrawingEnabled;
  }

  void SetNewTabTitle(const wxString &title) { newTabTitle = title; }
  wxString GetNewTabTitle() const {
    return newTabTitle.empty() ? "Terminal" : newTabTitle;
  }

  void SetShowCloseButton(bool show) { showCloseButton = show; }
  bool GetShowCloseButton() const { return showCloseButton; }

private:
  wxString themeName{"dark"};
  std::map<wxString, wxTerminalTheme> themes;
  wxFont font;
  wxString newTabTitle{"Terminal"};
  bool safeDrawingEnabled{false};
  bool showCloseButton{true};
};

class AppPersistence {
public:
  static wxString GetConfigPath();
  static bool Load(AppConfig &config);
  static bool Save(const AppConfig &config);
};
