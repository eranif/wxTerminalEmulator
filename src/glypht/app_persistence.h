#pragma once

#include "terminal_theme.h"
#include <map>
#include <vector>
#include <wx/font.h>
#include <wx/string.h>

/// A theme colour that the outline of the current tab can have.
struct TabOutlineColour {
  const char *key;   ///< The value in config.ini.
  const char *label; ///< The name in the settings dialog.
  wxColour wxTerminalTheme::*member;
};

/// Where the tabs are. Left and Right need wxWidgets 3.3 (see GlyphTabBar);
/// with an older wxWidgets they are shown at the top.
enum class TabPosition { Top, Bottom, Left, Right };

/// The look of the tabs (see GlyphTabArt::Style).
enum class TabStyle { Browser, Pills };

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

  /// The colours the outline of the current tab can have.
  static const std::vector<TabOutlineColour> &GetTabOutlineColours();
  /// The key of the outline colour of the current tab (see TabOutlineColour).
  /// An unknown key is kept, and means the default, magenta.
  void SetTabOutlineColour(const wxString &key) { tabOutlineColour = key; }
  const wxString &GetTabOutlineColour() const { return tabOutlineColour; }
  /// The colour of @p key in @p theme.
  static wxColour GetTabOutlineColourOf(const wxTerminalTheme &theme,
                                        const wxString &key);

  void SetTabPosition(TabPosition position) { tabPosition = position; }
  TabPosition GetTabPosition() const { return tabPosition; }
  /// The value of @p position in config.ini, and back. An unknown value is
  /// Top.
  static wxString TabPositionToString(TabPosition position);
  static TabPosition TabPositionFromString(const wxString &value);

  void SetTabStyle(TabStyle style) { tabStyle = style; }
  TabStyle GetTabStyle() const { return tabStyle; }
  /// The value of @p style in config.ini, and back. An unknown value is
  /// Browser.
  static wxString TabStyleToString(TabStyle style);
  static TabStyle TabStyleFromString(const wxString &value);

  /// The width of the vertical tab bar, in DIPs.
  void SetTabBarWidth(int width) { tabBarWidth = width; }
  int GetTabBarWidth() const { return tabBarWidth; }

  /// Optional shell to start in new tabs, instead of the default shell of the
  /// platform. Empty means: use the default shell.
  void SetShellExecutable(const wxString &path) { shellExecutable = path; }
  const wxString &GetShellExecutable() const { return shellExecutable; }
  /// Optional arguments for the shell above. Used only with a shell.
  void SetShellArgs(const wxString &args) { shellArgs = args; }
  const wxString &GetShellArgs() const { return shellArgs; }

  /// The command line the user picked last in the "New Terminal" prompt.
  void SetLastShell(const wxString &command) { lastShell = command; }
  const wxString &GetLastShell() const { return lastShell; }

  /// The command line to start the configured shell with: the executable
  /// (in double quotes when it has spaces), then the arguments. Empty when no
  /// shell is configured, or when the configured path is an absolute path of a
  /// file that does not exist (for example a removed shell).
  wxString GetShellCommand() const;

  void SetTabSwitcherSize(const wxSize &size) { tabSwitcherSize = size; }
  wxSize GetTabSwitcherSize() const { return tabSwitcherSize; }

private:
  const wxTerminalTheme *FindTheme(const wxString &name) const;

  wxString themeName{"dark"};
  std::map<wxString, wxTerminalTheme> themes;
  // Themes loaded from the shipped *.toml files. Read only.
  std::map<wxString, wxTerminalTheme> shippedThemes;
  wxFont font;
  wxString newTabTitle{"Terminal"};
  bool safeDrawingEnabled{true};
  bool showCloseButton{true};
  bool blockCursor{true};
  wxString tabOutlineColour{"magenta"};
  TabPosition tabPosition{TabPosition::Top};
  TabStyle tabStyle{TabStyle::Browser};
  int tabBarWidth{200};
  wxString shellExecutable;
  wxString shellArgs;
  wxString lastShell;
  wxSize tabSwitcherSize{520, 260};
};

class AppPersistence {
public:
  static wxString GetConfigPath();
  static bool Load(AppConfig &config);
  static bool Save(const AppConfig &config);
};
