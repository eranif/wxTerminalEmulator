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

  /// Store a theme. Storing a shipped theme with unchanged colours removes the
  /// user copy, so only edited shipped themes are saved to config.ini.
  void SetTheme(const wxString &name, const wxTerminalTheme &theme);
  /// Remove a user theme. A shipped theme with this name stays available.
  void RemoveTheme(const wxString &name);
  /// All theme names (user and shipped), sorted, without duplicates.
  std::vector<wxString> GetThemeNames() const;
  /// The themes to write to config.ini: built-in, user and edited shipped.
  std::vector<wxString> GetUserThemeNames() const;
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

  void SetBlockCursor(bool block) { blockCursor = block; }
  bool GetBlockCursor() const { return blockCursor; }

private:
  const wxTerminalTheme *FindTheme(const wxString &name) const;

  wxString themeName{"dark"};
  std::map<wxString, wxTerminalTheme> themes;
  // Themes loaded from the shipped *.toml files. Read only.
  std::map<wxString, wxTerminalTheme> shippedThemes;
  wxFont font;
  wxString newTabTitle{"Terminal"};
  bool safeDrawingEnabled{false};
  bool showCloseButton{true};
  bool blockCursor{true};
};

class AppPersistence {
public:
  static wxString GetConfigPath();
  static bool Load(AppConfig &config);
  static bool Save(const AppConfig &config);
};
