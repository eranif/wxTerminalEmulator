#pragma once

#include "app_persistence.h"
#include "terminal_event.h"
#include "terminal_logger.h"
#include "terminal_view.h"

#include <deque>
#include <optional>
#include <wx/app.h>
#include <wx/aui/auibook.h>
#include <wx/bitmap.h>
#include <wx/clipbrd.h>
#include <wx/cmdline.h>
#include <wx/display.h>
#include <wx/fdrepdlg.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/fontdlg.h>
#include <wx/frame.h>
#include <wx/icon.h>
#include <wx/iconbndl.h>
#include <wx/menu.h>
#include <wx/stdpaths.h>
#include <wx/string.h>
#include <wx/textdlg.h>
#include <wx/tokenzr.h>

using EnvironmentList = terminal::PtyBackend::EnvironmentList;

#ifndef WXTERMINAL_SVG_SOURCE_PATH
#define WXTERMINAL_SVG_SOURCE_PATH ""
#endif

static wxString GetAppIconPath() {
  wxString sourcePath = wxString::FromUTF8(WXTERMINAL_SVG_SOURCE_PATH);
  if (!sourcePath.empty() && wxFileExists(sourcePath)) {
    return sourcePath;
  }

  wxFileName iconPath(wxStandardPaths::Get().GetExecutablePath());
#if defined(__WXMAC__)
  wxFileName bundleIconPath = iconPath;
  bundleIconPath.RemoveLastDir();
  bundleIconPath.AppendDir("Resources");
  bundleIconPath.SetFullName("wxterminal.svg");
  if (wxFileExists(bundleIconPath.GetFullPath())) {
    return bundleIconPath.GetFullPath();
  }
#endif
  iconPath.SetFullName("wxterminal.svg");
  return iconPath.GetFullPath();
}

static wxIconBundle LoadAppIcons() {
  const wxBitmapBundle svgBundle =
      wxBitmapBundle::FromSVGFile(GetAppIconPath(), wxSize(256, 256));
  wxIconBundle icons;

  if (!svgBundle.IsOk()) {
    return icons;
  }

  for (int size : {16, 32, 48, 64, 128, 256}) {
    wxIcon icon;
    icon.CopyFromBitmap(svgBundle.GetBitmap(wxSize(size, size)));
    if (icon.IsOk()) {
      icons.AddIcon(icon);
    }
  }

  return icons;
}

class MyTerminal : public wxTerminalViewCtrl {
public:
  MyTerminal(wxAuiNotebook *parent, const wxString &shellCommand,
             const std::optional<EnvironmentList> &environment,
             std::optional<wxString> workingDirectory = std::nullopt,
             bool showScrollBar = true)
      : wxTerminalViewCtrl(parent, shellCommand, environment, workingDirectory,
                           showScrollBar),
        m_notebook{parent} {
    Bind(wxEVT_TERMINAL_TITLE_CHANGED, &MyTerminal::OnTitleChanged, this);
  }

  ~MyTerminal() override = default;

  void OnTitleChanged(wxTerminalEvent &event) {
    if (!m_customLabel.empty()) {
      return;
    }

    wxString title = event.GetTitle();
    title.Trim().Trim(false);
    if (title.empty()) {
      title = _("Terminal");
    }
    int sel = m_notebook->FindPage(this);
    if (sel != wxNOT_FOUND) {
      m_notebook->SetPageText(sel, title);
    }
  }

  void SetTabLabel(const wxString &label) {
    wxString l = label;
    l.Trim().Trim(false);
    if (l.empty()) {
      return;
    }
    m_customLabel = l;
    int sel = m_notebook->FindPage(this);
    if (sel != wxNOT_FOUND) {
      m_notebook->SetPageText(sel, m_customLabel);
    }
  }

  void ResetTabLabel() {
    if (m_customLabel.empty()) {
      return;
    }
    m_customLabel.clear();
    int sel = m_notebook->FindPage(this);
    if (sel != wxNOT_FOUND) {
      m_notebook->SetPageText(sel, _("Terminal"));
    }
  }

private:
  wxAuiNotebook *m_notebook{nullptr};
  wxString m_customLabel;
};

class MyFrame : public wxFrame {
public:
  enum {
    ID_ThemeDark = wxID_HIGHEST + 1,
    ID_ThemeLight,
    ID_ChangeFont,
    ID_CenterLine,
    ID_SafeDrawing,
    ID_SetSelection,
    ID_PrintLine,
    ID_SendInput,
    ID_NewTerminal,
    ID_FindText,
    ID_CopyAll,
    ID_Exit
  };

  struct TerminalPageConfig {
    wxString shellCommand;
    std::optional<EnvironmentList> environment;
    std::optional<wxString> workingDirectory;
  };

  MyFrame(const wxCmdLineParser &parser,
          const std::optional<EnvironmentList> &environment,
          std::optional<wxString> workingDirectory = std::nullopt);

  void ShowTabMenu();
  static std::optional<EnvironmentList> ParseEnvironmentList(const wxString &s);
  void BuildMenuBar();
  void OnNextTabUI(wxUpdateUIEvent &event);
  void OnPreviousTabUI(wxUpdateUIEvent &event);
  void OnNextTab(wxCommandEvent &event);
  void OnPreviousTab(wxCommandEvent &event);
  void OnCloseTab(wxCommandEvent &event);
  void ApplyThemeToAllTabs(const wxTerminalTheme &theme);
  void ApplyFontToAllTabs(const wxFont &font);
  wxTerminalViewCtrl *GetActiveTerminalView() const;
  MyTerminal *CreateTerminalPage(const TerminalPageConfig &config);
  void ApplySafeDrawingToAllTabs(bool enabled);
  void UpdateSafeDrawingMenuCheck();
  void ApplyThemeToTab(wxTerminalViewCtrl *view);
  void OnNewTerminal(wxCommandEvent &event);
  void OnExit(wxCommandEvent &event);
  void OnDarkTheme(wxCommandEvent &event);
  void OnLightTheme(wxCommandEvent &event);
  void OnChangeFont(wxCommandEvent &event);
  void OnCenterLine(wxCommandEvent &event);
  void OnSafeDrawing(wxCommandEvent &event);
  void OnSetSelection(wxCommandEvent &event);
  void OnPrintLine(wxCommandEvent &event);
  void OnSendInput(wxCommandEvent &event);
  void OnCopyAll(wxCommandEvent &event);
  void OnFindText(wxCommandEvent &event);
  void OnFindNext(wxFindDialogEvent &event);
  void OnTerminated(wxTerminalEvent &event);
  void OnTerminalLink(wxTerminalEvent &event);
  void OnBell(wxTerminalEvent &event);
  void OnTimer(wxTimerEvent &event);
  void Terminate();
  void PersistSettings();
  void ApplyNativeAppTheme(std::optional<bool> darkMode = std::nullopt);

private:
  wxAuiNotebook *m_notebook{nullptr};
  MyTerminal *m_view{nullptr};
  wxString m_defaultShellCommand;
  std::optional<EnvironmentList> m_defaultEnvironment;
  std::optional<wxString> m_defaultWorkingDirectory;
  bool m_themeIsDark{true};
  wxFont m_persistedFont;
  wxString m_currentSearchText;
  bool m_safeDrawingEnabled{false};
  wxTimer m_timer;
  std::deque<std::function<void()>> m_timerCallbacks;
  wxFindReplaceDialog *m_findDialog{nullptr};
  wxFindReplaceData m_findReplaceData;
};
