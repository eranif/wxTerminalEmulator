#pragma once

#include "BookLayout.h"
#include "GlyphBook.h"
#include "TabMRU.h"
#include "TabSwitcherDlg.h"
#include "app_persistence.h"
#include "layout_persistence.h"
#include "shell_locator.h"
#include "terminal_event.h"
#include "terminal_logger.h"
#include "terminal_view.h"

#include <deque>
#include <optional>
#include <vector>
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

// The tabbed book of the window. With wxWidgets 3.3 it can have tabs on any
// side; before that it is a wxAuiNotebook (tabs at the top or at the bottom).
#if wxCHECK_VERSION(3, 3, 0)
using TabBook = GlyphBook;
using TabBookEvent = wxBookCtrlEvent;
#define TAB_BOOK_PAGE_CHANGED wxEVT_BOOKCTRL_PAGE_CHANGED
#define TAB_BOOK_TAB_RIGHT_UP wxEVT_GLYPHBOOK_TAB_RIGHT_UP
#else
using TabBook = wxAuiNotebook;
using TabBookEvent = wxAuiNotebookEvent;
#define TAB_BOOK_PAGE_CHANGED wxEVT_AUINOTEBOOK_PAGE_CHANGED
#define TAB_BOOK_TAB_RIGHT_UP wxEVT_AUINOTEBOOK_TAB_RIGHT_UP
#endif

// The parent for the pages of @p book.
inline wxWindow *PagesParentOf(TabBook *book) {
#if wxCHECK_VERSION(3, 3, 0)
  return book->GetPagesParent();
#else
  return book;
#endif
}

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
  bundleIconPath.SetFullName("glypht.svg");
  if (wxFileExists(bundleIconPath.GetFullPath())) {
    return bundleIconPath.GetFullPath();
  }
#endif
  iconPath.SetFullName("glypht.svg");
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
  // @p parent is the panel of the tab (see TerminalPanel), which is the page of
  // @p notebook.
  MyTerminal(wxWindow *parent, TabBook *notebook,
             const wxString &shellCommand,
             const std::optional<EnvironmentList> &environment,
             std::optional<wxString> workingDirectory = std::nullopt,
             bool showScrollBar = true)
      : wxTerminalViewCtrl(parent, shellCommand, environment, workingDirectory,
                           showScrollBar),
        m_notebook{notebook} {
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
    int sel = m_notebook->FindPage(GetParent());
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
    int sel = m_notebook->FindPage(GetParent());
    if (sel != wxNOT_FOUND) {
      m_notebook->SetPageText(sel, m_customLabel);
    }
  }

  void ResetTabLabel() {
    if (m_customLabel.empty()) {
      return;
    }
    m_customLabel.clear();
    int sel = m_notebook->FindPage(GetParent());
    if (sel != wxNOT_FOUND) {
      m_notebook->SetPageText(sel, _("Terminal"));
    }
  }

  // The user-assigned tab label, or empty if the title is driven by the shell.
  const wxString &GetCustomLabel() const { return m_customLabel; }

private:
  TabBook *m_notebook{nullptr};
  wxString m_customLabel;
};

// A tab of the notebook: a panel with a vertical sizer that holds the terminal.
// The sizer lets the terminal put its progress bar above itself (the notebook
// is a wxBookCtrlBase, so the terminal can't use it as the parent for that).
class TerminalPanel : public wxPanel {
public:
  TerminalPanel(TabBook *notebook, const wxString &shellCommand,
                const std::optional<EnvironmentList> &environment,
                std::optional<wxString> workingDirectory)
      : wxPanel(PagesParentOf(notebook)) {
    m_terminal = new MyTerminal(this, notebook, shellCommand, environment,
                                workingDirectory);
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_terminal, wxSizerFlags(1).Expand());
    SetSizer(sizer);
    // After SetSizer(): the progress bar is added to the sizer of the parent.
    m_terminal->ShowProgressBar(true);
  }

  MyTerminal *GetTerminal() const { return m_terminal; }

  // The terminal takes the focus of the tab.
  void SetFocus() override { m_terminal->SetFocus(); }

private:
  MyTerminal *m_terminal{nullptr};
};

class MyFrame : public wxFrame {
public:
  enum {
    ID_CenterLine = wxID_HIGHEST + 1,
    ID_SetSelection,
    ID_PrintLine,
    ID_SendInput,
    ID_NewTerminal,
    ID_FindText,
    ID_TabSwitcherNext,
    ID_TabSwitcherPrev,
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
  ~MyFrame() override;

  // Show the context menu of a tab. @p tabIdx is the tab that was clicked.
  void ShowTabMenu(int tabIdx = wxNOT_FOUND);
  // Ask for a text and show it as the header (or the footer) strip of a tab,
  // with random colours. An empty text keeps the current strip.
  void SetTabStrip(size_t tabIdx, bool footer);
  static std::optional<EnvironmentList> ParseEnvironmentList(const wxString &s);
  void BuildMenuBar();
  void OnNextTabUI(wxUpdateUIEvent &event);
  void OnPreviousTabUI(wxUpdateUIEvent &event);
  void OnNextTab(wxCommandEvent &event);
  void OnPreviousTab(wxCommandEvent &event);
  void OnCloseTab(wxCommandEvent &event);
  // Show the modeless tab list in most-recently-used order (Ctrl-Tab).
  // Returns false if there is nothing to switch to (fewer than 2 tabs).
  bool ShowTabSwitcher(bool backwards);
  void ApplySettings();
  wxTerminalViewCtrl *GetActiveTerminalView() const;
  // The terminal of a notebook page (a TerminalPanel), or nullptr.
  static MyTerminal *TerminalOf(wxWindow *page);
  MyTerminal *CreateTerminalPage(const TerminalPageConfig &config,
                                 bool selectIt);
  void ApplyThemeToTab(wxTerminalViewCtrl *view);
  // Set the theme of every terminal tab, and of the tabs themselves with the
  // tab outline colour @p outlineColour (a key of AppConfig).
  void ApplyThemeToAllTabs(const wxTerminalTheme &theme,
                           const wxString &outlineColour);
  void OnNewTerminal(wxCommandEvent &event);
  // Ask which shell to start for a new tab (Windows only; elsewhere the default
  // shell is used). Returns nullopt if the user cancelled.
  std::optional<wxString> PromptForShell();
  void OnExit(wxCommandEvent &event);
  void OnCenterLine(wxCommandEvent &event);
  void OnSetSelection(wxCommandEvent &event);
  void OnPrintLine(wxCommandEvent &event);
  void OnSendInput(wxCommandEvent &event);
  void OnSettings(wxCommandEvent &event);
  void OnCopyAll(wxCommandEvent &event);
  void OnFindText(wxCommandEvent &event);
  void OnFindNext(wxFindDialogEvent &event);
  void OnTerminated(wxTerminalEvent &event);
  void OnTerminalLink(wxTerminalEvent &event);
  void OnBell(wxTerminalEvent &event);
  void OnCwdChanged(wxTerminalEvent &event);
  // Show the directory of the active terminal in the status bar.
  void UpdateDirectoryStatus();
  void OnProgress(wxTerminalEvent &event);
  // Show the last progress or program status event of the active terminal.
  void UpdateProgressStatus();
  void OnAppSelectionChanged(wxTerminalEvent &event);
  // Show the text the program in the active terminal copied with OSC 52.
  void UpdateAppSelectionStatus();
  void OnTimer(wxTimerEvent &event);
  void Terminate();
  void PersistSettings();
  // The shell for new tabs: --shell if given, else the shell of the settings,
  // else empty (the default shell of the platform).
  wxString ResolveDefaultShell() const;
  void ApplyNativeAppTheme();

  // Serialize the open tabs and their AUI arrangement to disk. Called on exit.
  void SaveLayout();
  // Recreate the tabs saved by SaveLayout(). Returns true if a layout was
  // restored (at least one tab created).
  bool RestoreLayout();
  void OnClose(wxCloseEvent &event);

private:
  TabBook *m_notebook{nullptr};
  // The tabs in most-recently-used order.
  TabMRU m_tabMru;
  TabSwitcherDlg *m_tabSwitcher{nullptr};
  // The shells found on this computer. Filled on first use.
  std::optional<std::vector<ShellOption>> m_shellOptions;
  // The shell from --shell. It wins over the shell of the settings.
  wxString m_cliShellCommand;
  // --appearance=light|dark: true for dark. Empty for auto, then the app
  // follows the background of the terminal theme.
  std::optional<bool> m_cliForceDark;
  wxString m_defaultShellCommand;
  std::optional<EnvironmentList> m_defaultEnvironment;
  std::optional<wxString> m_defaultWorkingDirectory;
  AppConfig m_config;
  wxString m_currentSearchText;
  bool m_bellCallbackInstalled{false};
  wxTimer m_timer;
  // queue of commands to run in the timer handler. If a callback returns false
  // it is executed and then re-installed in the queue, else it is discarded
  // (after being executed).
  std::deque<std::function<bool()>> m_timerCallbacks;
  wxFindReplaceDialog *m_findDialog{nullptr};
  wxFindReplaceData m_findReplaceData;
};
