#include "app_persistence.h"
#include "terminal_event.h"
#include "terminal_logger.h"
#include "terminal_view.h"
#include <deque>
#include <optional>
#include <stdexcept>
#include <wx/app.h>
#include <wx/aui/auibook.h>
#include <wx/bmpbndl.h>
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
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/stdpaths.h>
#include <wx/string.h>
#include <wx/sysopt.h>
#include <wx/textdlg.h>
#include <wx/tokenzr.h>
#include <wx/xrc/xmlres.h>

#ifndef WXTERMINAL_SVG_SOURCE_PATH
#define WXTERMINAL_SVG_SOURCE_PATH ""
#endif

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
    ID_Exit
  };

  using EnvironmentList = terminal::PtyBackend::EnvironmentList;

  struct TerminalPageConfig {
    wxString shellCommand;
    std::optional<EnvironmentList> environment;
    std::optional<wxString> workingDirectory;
  };

  MyFrame(const wxCmdLineParser &parser,
          const std::optional<EnvironmentList> &environment,
          std::optional<wxString> workingDirectory = std::nullopt)
#if USE_OPENGL
      : wxFrame(nullptr, wxID_ANY, "wxTerminalEmulator (OpenGL)") {
#else
      : wxFrame(nullptr, wxID_ANY, "wxTerminalEmulator") {
#endif
    SetIcons(LoadAppIcons());
    wxString shellCommand;
    parser.Found("shell", &shellCommand);

    wxString title;
    parser.Found("title", &title);

    if (!title.empty()) {
      SetLabel(title);
    }

    wxString command;
    parser.Found("command", &command);

    m_timer.SetOwner(this);
    Bind(wxEVT_TIMER, &MyFrame::OnTimer, this, m_timer.GetId());
    m_timer.StartOnce(1000);

    // Get the primary display size
    wxDisplay display(wxDisplay::GetFromWindow(this));
    wxRect screen = display.GetClientArea();

    // Set initial size to 1/3 of screen dimensions
    int width = screen.width / 2;
    int height = screen.height / 2;
    SetSize(width, height);
    CentreOnScreen(); // Center the window on screen

    wxString persistedTheme = "dark";
    wxFont persistedFont;
    bool persistedSafeDrawing = false;
    if (AppPersistence::Load(persistedTheme, persistedFont)) {
      m_themeIsDark = (persistedTheme.Lower() != "light");
      m_persistedFont = persistedFont;
    } else {
      m_themeIsDark = true;
    }

    AppPersistence::Load(persistedSafeDrawing);
    m_safeDrawingEnabled = persistedSafeDrawing;

#if USE_OPENGL
    m_safeDrawingEnabled = true;
#endif

    ApplyNativeAppTheme();

    BuildMenuBar();
    m_notebook =
        new wxAuiNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                          wxAUI_NB_DEFAULT_STYLE | wxAUI_NB_CLOSE_ON_ALL_TABS);
    m_defaultShellCommand = shellCommand;
    m_defaultEnvironment = environment;
    m_defaultWorkingDirectory = std::move(workingDirectory);
    m_view = CreateTerminalPage(
        {shellCommand, environment, m_defaultWorkingDirectory});
    m_notebook->Bind(
        wxEVT_AUINOTEBOOK_PAGE_CHANGED, [this](wxAuiNotebookEvent &event) {
          event.Skip();
          if (m_notebook->GetPageCount() == 0) {
            return;
          }
          auto page = m_notebook->GetPage(m_notebook->GetSelection());
          if (page) {
            page->SetFocus();
          }
        });
    m_notebook->Bind(wxEVT_AUINOTEBOOK_TAB_RIGHT_UP,
                     [this](wxAuiNotebookEvent &event) {
                       CallAfter(&MyFrame::ShowTabMenu);
                     });
    if (m_view && !command.empty()) {
      m_view->SendCommand(command);
    }
  }

  void ShowTabMenu() {
    wxMenu menu;
    menu.Append(XRCID("rename-tab"), _("Set Label..."));
    menu.Append(wxID_CLEAR, _("Reset Label..."));
    menu.Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          int tabIdx = m_notebook->HitTest(::wxGetMousePosition());
          if (tabIdx == wxNOT_FOUND) {
            return;
          }
          wxString name = ::wxGetTextFromUser(_("New Tab Name:"));
          if (name.empty()) {
            return;
          }
          auto *terminal = dynamic_cast<MyTerminal *>(
              m_notebook->GetPage(static_cast<size_t>(tabIdx)));
          if (terminal == nullptr) {
            return;
          }
          terminal->SetTabLabel(name);
        },
        XRCID("rename-tab"));
    menu.Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) {
          int tabIdx = m_notebook->HitTest(::wxGetMousePosition());
          if (tabIdx == wxNOT_FOUND) {
            return;
          }
          auto *terminal = dynamic_cast<MyTerminal *>(
              m_notebook->GetPage(static_cast<size_t>(tabIdx)));
          if (terminal == nullptr) {
            return;
          }
          terminal->ResetTabLabel();
        },
        wxID_CLEAR);
    m_notebook->PopupMenu(&menu);
  }

  static std::optional<EnvironmentList>
  ParseEnvironmentList(const wxString &s) {
    if (s.empty()) {
      return EnvironmentList{};
    }

    EnvironmentList env;
    wxStringTokenizer tokens(s, ",", wxTOKEN_RET_EMPTY_ALL);
    while (tokens.HasMoreTokens()) {
      wxString token = tokens.GetNextToken().Strip(wxString::both);
      if (token.empty() || !token.Contains('='))
        continue;
      env.push_back(token.ToStdString(wxConvUTF8));
    }
    return env;
  }

  void BuildMenuBar() {
    auto *menuBar = new wxMenuBar();
    auto *fileMenu = new wxMenu();
    fileMenu->Append(wxID_NEW, _("New Terminal\tCtrl-N"));
    fileMenu->Append(wxID_CLOSE, _("Close Terminal\tCtrl-W"));
    fileMenu->AppendSeparator();
    fileMenu->Append(ID_Exit, "Exit");
    menuBar->Append(fileMenu, "File");

    auto *optionsMenu = new wxMenu();
    optionsMenu->AppendRadioItem(ID_ThemeDark, "Dark theme");
    optionsMenu->AppendRadioItem(ID_ThemeLight, "Light theme");
    optionsMenu->AppendSeparator();
    optionsMenu->Append(ID_ChangeFont, "Change Font...");
    optionsMenu->Append(ID_CenterLine, "Center Line...");
    optionsMenu->AppendCheckItem(ID_SafeDrawing, "Safe Drawing");
    optionsMenu->Append(ID_SetSelection, "Set Selection...");
    optionsMenu->Append(ID_PrintLine, "Print Line...");
    optionsMenu->Append(ID_SendInput, "Send Input...");
    optionsMenu->Check(m_themeIsDark ? ID_ThemeDark : ID_ThemeLight, true);
    menuBar->Append(optionsMenu, "Options");

    auto *searchMenu = new wxMenu();
    searchMenu->Append(wxID_FORWARD, _("Next Tab\tCtrl-RIGHT"));
    searchMenu->Append(wxID_BACKWARD, _("Previous Tab\tCtrl-LEFT"));
    searchMenu->AppendSeparator();
    searchMenu->Append(ID_FindText, "Find Text...\tCtrl-F");
    menuBar->Append(searchMenu, "Search");

    SetMenuBar(menuBar);

    Bind(wxEVT_MENU, &MyFrame::OnNewTerminal, this, wxID_NEW);
    Bind(wxEVT_MENU, &MyFrame::OnCloseTab, this, wxID_CLOSE);
    Bind(wxEVT_MENU, &MyFrame::OnNextTab, this, wxID_FORWARD);
    Bind(wxEVT_MENU, &MyFrame::OnPreviousTab, this, wxID_BACKWARD);
    Bind(wxEVT_MENU, &MyFrame::OnExit, this, ID_Exit);
    Bind(wxEVT_MENU, &MyFrame::OnDarkTheme, this, ID_ThemeDark);
    Bind(wxEVT_MENU, &MyFrame::OnLightTheme, this, ID_ThemeLight);
    Bind(wxEVT_MENU, &MyFrame::OnChangeFont, this, ID_ChangeFont);
    Bind(wxEVT_MENU, &MyFrame::OnCenterLine, this, ID_CenterLine);
    Bind(wxEVT_MENU, &MyFrame::OnSafeDrawing, this, ID_SafeDrawing);
    Bind(wxEVT_MENU, &MyFrame::OnSetSelection, this, ID_SetSelection);
    Bind(wxEVT_MENU, &MyFrame::OnPrintLine, this, ID_PrintLine);
    Bind(wxEVT_MENU, &MyFrame::OnSendInput, this, ID_SendInput);
    Bind(wxEVT_MENU, &MyFrame::OnFindText, this, ID_FindText);
    Bind(wxEVT_UPDATE_UI, &MyFrame::OnNextTabUI, this, wxID_FORWARD);
    Bind(wxEVT_UPDATE_UI, &MyFrame::OnPreviousTabUI, this, wxID_BACKWARD);
  }

  void OnNextTabUI(wxUpdateUIEvent &event) {
    event.Enable(m_notebook->GetPageCount() > 1);
  }
  void OnPreviousTabUI(wxUpdateUIEvent &event) {
    event.Enable(m_notebook->GetPageCount() > 1);
  }

  void OnNextTab(wxCommandEvent &event) {
    if (m_notebook->GetPageCount() == 0) {
      return;
    }
    int currentTabIndex = m_notebook->GetSelection();
    if (currentTabIndex == wxNOT_FOUND) {
      return;
    }

    if (currentTabIndex + 1 >= m_notebook->GetPageCount()) {
      // cant move forward, cycle
      currentTabIndex = 0;
    } else {
      currentTabIndex += 1;
    }
    m_notebook->SetSelection(currentTabIndex);
  }

  void OnPreviousTab(wxCommandEvent &event) {
    if (m_notebook->GetPageCount() == 0) {
      return;
    }
    int currentTabIndex = m_notebook->GetSelection();
    if (currentTabIndex == wxNOT_FOUND) {
      return;
    }

    if (currentTabIndex == 0) {
      currentTabIndex = m_notebook->GetPageCount() - 1;
    } else {
      currentTabIndex -= 1;
    }
    m_notebook->SetSelection(currentTabIndex);
  }

  void OnCloseTab(wxCommandEvent &event) {
    int currentTabIndex = m_notebook->GetSelection();
    if (currentTabIndex == wxNOT_FOUND) {
      return;
    }
    m_notebook->DeletePage(static_cast<size_t>(currentTabIndex));
  }

  void ApplyThemeToAllTabs(const wxTerminalTheme &theme) {
    if (!m_notebook) {
      return;
    }

    for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
      if (auto *view =
              dynamic_cast<wxTerminalViewCtrl *>(m_notebook->GetPage(i))) {
        view->SetTheme(theme);
        view->Refresh();
      }
    }
  }

  void ApplyFontToAllTabs(const wxFont &font) {
    if (!m_notebook) {
      return;
    }

    for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
      if (auto *view =
              dynamic_cast<wxTerminalViewCtrl *>(m_notebook->GetPage(i))) {
        wxTerminalTheme theme = view->GetTheme();
        theme.font = font;
        view->SetTheme(theme);
        view->Refresh();
      }
    }
  }

  wxTerminalViewCtrl *GetActiveTerminalView() const {
    if (!m_notebook || m_notebook->GetPageCount() == 0) {
      return nullptr;
    }
    return dynamic_cast<wxTerminalViewCtrl *>(m_notebook->GetCurrentPage());
  }

  MyTerminal *CreateTerminalPage(const TerminalPageConfig &config) {
    auto *page = new MyTerminal(m_notebook, config.shellCommand,
                                config.environment, config.workingDirectory);
    m_notebook->AddPage(page, "Terminal", true);

    ApplyThemeToTab(page);
    // OpenGL always uses Safe-Drawing (Per Cell Rendering)
    m_safeDrawingEnabled =
        m_safeDrawingEnabled || wxTerminalViewCtrl::IsOpenGLEnabled();
    page->EnableSafeDrawing(m_safeDrawingEnabled);
    UpdateSafeDrawingMenuCheck();
    page->Bind(wxEVT_TERMINAL_TERMINATED, &MyFrame::OnTerminated, this);
    page->Bind(wxEVT_TERMINAL_TEXT_LINK, &MyFrame::OnTerminalLink, this);
    page->Bind(wxEVT_TERMINAL_BELL, &MyFrame::OnBell, this);
    return page;
  }

  void ApplySafeDrawingToAllTabs(bool enabled) {
    if (!m_notebook) {
      return;
    }

    for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
      if (auto *view =
              dynamic_cast<wxTerminalViewCtrl *>(m_notebook->GetPage(i))) {
        view->EnableSafeDrawing(enabled);
        view->Refresh();
      }
    }
  }

  void UpdateSafeDrawingMenuCheck() {
    if (auto *menuBar = GetMenuBar()) {
      auto *optionsMenu = menuBar->GetMenu(1);
      if (optionsMenu && optionsMenu->FindItem(ID_SafeDrawing)) {
#if USE_OPENGL
        optionsMenu->Enable(ID_SafeDrawing, false);
        optionsMenu->Check(ID_SafeDrawing, true);
#else
        optionsMenu->Check(ID_SafeDrawing, m_safeDrawingEnabled);
#endif
      }
    }
  }

  void ApplyThemeToTab(wxTerminalViewCtrl *view) {
    if (!view) {
      return;
    }

    auto theme = m_themeIsDark ? wxTerminalTheme::MakeDarkTheme()
                               : wxTerminalTheme::MakeLightTheme();
    if (m_persistedFont.IsOk()) {
      theme.font = m_persistedFont;
    }
    view->SetTheme(theme);
  }

  void OnNewTerminal(wxCommandEvent &event) {
    wxUnusedVar(event);
    CreateTerminalPage({m_defaultShellCommand, m_defaultEnvironment,
                        m_defaultWorkingDirectory});
    m_notebook->SetSelection(m_notebook->GetPageCount() - 1);
  }

  void OnExit(wxCommandEvent &event) {
    wxUnusedVar(event);
    Close(true);
  }

  void OnDarkTheme(wxCommandEvent &event) {
    wxUnusedVar(event);
    ApplyNativeAppTheme(true);
    auto theme = wxTerminalTheme::MakeDarkTheme();
    if (m_persistedFont.IsOk()) {
      theme.font = m_persistedFont;
    }
    ApplyThemeToAllTabs(theme);
    m_themeIsDark = true;
    PersistSettings();
  }

  void OnLightTheme(wxCommandEvent &event) {
    wxUnusedVar(event);
    ApplyNativeAppTheme(false);
    auto theme = wxTerminalTheme::MakeLightTheme();
    if (m_persistedFont.IsOk()) {
      theme.font = m_persistedFont;
    }
    ApplyThemeToAllTabs(theme);
    m_themeIsDark = false;
    PersistSettings();
  }

  void OnChangeFont(wxCommandEvent &event) {
    wxUnusedVar(event);
    wxTerminalViewCtrl *activeView = GetActiveTerminalView();
    if (!activeView) {
      return;
    }

    wxFontData fontData;
    fontData.EnableEffects(false);
    fontData.SetInitialFont(
        m_persistedFont.IsOk() ? m_persistedFont : activeView->GetTheme().font);

    wxFontDialog dlg(this, fontData);
    if (dlg.ShowModal() != wxID_OK) {
      return;
    }

    m_persistedFont = dlg.GetFontData().GetChosenFont();
    ApplyFontToAllTabs(m_persistedFont);
    PersistSettings();
  }

  void OnCenterLine(wxCommandEvent &event) {
    wxUnusedVar(event);
    wxTerminalViewCtrl *activeView = GetActiveTerminalView();
    if (!activeView) {
      return;
    }

    const std::size_t totalLines = activeView->GetLineCount();
    const wxString prompt = wxString::Format("Enter line number (1-%zu):",
                                             totalLines > 0 ? totalLines : 1);

    wxString value =
        wxGetTextFromUser(prompt, "Center Line", wxEmptyString, this);
    if (value.empty()) {
      return;
    }

    long lineNumber = 0;
    if (!value.ToLong(&lineNumber) || lineNumber < 1 ||
        static_cast<std::size_t>(lineNumber) > totalLines) {
      wxMessageBox("Invalid line number.", "Center Line", wxOK | wxICON_WARNING,
                   this);
      return;
    }

    activeView->CenterLine(static_cast<std::size_t>(lineNumber - 1));
  }

  void OnSafeDrawing(wxCommandEvent &event) {
    m_safeDrawingEnabled = event.IsChecked();
    ApplySafeDrawingToAllTabs(m_safeDrawingEnabled);
    PersistSettings();
  }

  void OnSetSelection(wxCommandEvent &event) {
    wxUnusedVar(event);
    wxTerminalViewCtrl *activeView = GetActiveTerminalView();
    if (!activeView) {
      return;
    }

    const std::size_t totalLines = activeView->GetLineCount();

    wxString lineStr =
        wxGetTextFromUser(wxString::Format("Enter line number (1-%zu):",
                                           totalLines > 0 ? totalLines : 1),
                          "Set Selection", wxEmptyString, this);
    if (lineStr.empty()) {
      return;
    }

    wxString colStr = wxGetTextFromUser(
        "Enter column start (1-based):", "Set Selection", wxEmptyString, this);
    if (colStr.empty()) {
      return;
    }

    wxString countStr = wxGetTextFromUser(
        "Enter number of chars:", "Set Selection", wxEmptyString, this);
    if (countStr.empty()) {
      return;
    }

    long lineNumber = 0;
    long colStart = 0;
    long count = 0;
    if (!lineStr.ToLong(&lineNumber) || lineNumber < 1 ||
        static_cast<std::size_t>(lineNumber) > totalLines ||
        !colStr.ToLong(&colStart) || colStart < 1 || !countStr.ToLong(&count) ||
        count < 1) {
      wxMessageBox("Invalid selection parameters.", "Set Selection",
                   wxOK | wxICON_WARNING, this);
      return;
    }

    activeView->SetUserSelection(static_cast<std::size_t>(lineNumber - 1),
                                 static_cast<std::size_t>(colStart - 1),
                                 static_cast<std::size_t>(count));
  }

  void OnPrintLine(wxCommandEvent &event) {
    wxUnusedVar(event);
    wxTerminalViewCtrl *activeView = GetActiveTerminalView();
    if (!activeView) {
      return;
    }

    const std::size_t totalLines = activeView->GetLineCount();
    wxString lineStr =
        wxGetTextFromUser(wxString::Format("Enter line number (1-%zu):",
                                           totalLines > 0 ? totalLines : 1),
                          "Print Line", wxEmptyString, this);
    if (lineStr.empty()) {
      return;
    }

    long lineNumber = 0;
    if (!lineStr.ToLong(&lineNumber) || lineNumber < 1 ||
        static_cast<std::size_t>(lineNumber) > totalLines) {
      wxMessageBox("Invalid line number.", "Print Line", wxOK | wxICON_WARNING,
                   this);
      return;
    }

    wxString content =
        activeView->GetLine(static_cast<std::size_t>(lineNumber - 1));
    wxMessageBox(content.empty() ? "<empty line>" : content, "Line Content",
                 wxOK | wxICON_INFORMATION, this);
  }

  void OnSendInput(wxCommandEvent &event) {
    wxUnusedVar(event);
    wxTerminalViewCtrl *activeView = GetActiveTerminalView();
    if (!activeView) {
      return;
    }

    wxString input = wxGetTextFromUser(
        "Enter text to send to terminal:", "Send Input", wxEmptyString, this);
    if (input.empty()) {
      return;
    }
    activeView->SendCommand(input);
  }

  void OnFindText(wxCommandEvent &event) {
    wxTerminalViewCtrl *activeView = GetActiveTerminalView();
    if (!activeView) {
      return;
    }

    if (m_findDialog != nullptr) {
      m_findDialog->Destroy();
      m_findDialog = nullptr;
    }

    if (m_findDialog == nullptr) {
      m_findReplaceData.SetFlags(wxFindReplaceFlags::wxFR_DOWN);
      m_findDialog =
          new wxFindReplaceDialog(this, &m_findReplaceData, _("Find Text"));
      m_findDialog->Bind(wxEVT_FIND_NEXT, &MyFrame::OnFindNext, this);
    }
    m_findDialog->Show();
  }

  void OnFindNext(wxFindDialogEvent &event) {
    wxUnusedVar(event);
    wxTerminalViewCtrl *activeView = GetActiveTerminalView();
    if (!activeView) {
      return;
    }

    size_t flags{0};
    if (event.GetFlags() & wxFindReplaceFlags::wxFR_DOWN) {
      flags |= wxTerminalViewCtrl::kForward;
    } else {
      flags |= wxTerminalViewCtrl::kBackward;
    }

    if (!(event.GetFlags() & wxFindReplaceFlags::wxFR_MATCHCASE)) {
      flags |= wxTerminalViewCtrl::kCaseInSensitive;
    }

    activeView->FindText(event.GetFindString(), flags);
  }

  void OnTerminated(wxTerminalEvent &event) {
    auto *view = dynamic_cast<wxTerminalViewCtrl *>(event.GetEventObject());
    if (!view || !m_notebook) {
      return;
    }

    int tabIndex = wxNOT_FOUND;
    for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
      if (m_notebook->GetPage(i) == view) {
        tabIndex = static_cast<int>(i);
        break;
      }
    }

    if (tabIndex == wxNOT_FOUND) {
      return;
    }

    if (!m_notebook || m_notebook->GetPageCount() == 0) {
      return;
    }

    if (tabIndex >= 0 &&
        tabIndex < static_cast<int>(m_notebook->GetPageCount())) {
      m_notebook->DeletePage(static_cast<size_t>(tabIndex));
    }
  }

  void OnTerminalLink(wxTerminalEvent &event) {
    event.Skip();
    wxString clickedText = event.GetClickedText();
    if (clickedText.StartsWith("http://") ||
        clickedText.StartsWith("https://")) {
      ::wxLaunchDefaultBrowser(clickedText);
    } else if (clickedText.StartsWith("file://")) {
      ::wxLaunchDefaultApplication(clickedText);
    } else if (wxFileName::FileExists(clickedText)) {
      // file
      ::wxLaunchDefaultApplication(clickedText);
    }
  }

  void OnBell(wxTerminalEvent &event) {
    auto view = dynamic_cast<wxTerminalViewCtrl *>(event.GetEventObject());
    if (!view) {
      return;
    }
    if (!m_notebook) {
      return;
    }
    int sel = m_notebook->FindPage(view);
    if (sel != wxNOT_FOUND) {
      return;
    }

    wxString page_title = m_notebook->GetPageText(sel);
    m_notebook->SetPageText(sel, page_title + wxT(" 🚨 "));

    auto restore_title = [view, page_title, this]() {
      int where = m_notebook->FindPage(view);
      if (where == wxNOT_FOUND) {
        return;
      }
      m_notebook->SetPageText(where, page_title);
    };
    m_timerCallbacks.push_back(std::move(restore_title));
  }

  void OnTimer(wxTimerEvent &event) {
    while (!m_timerCallbacks.empty()) {
      auto cb = std::move(m_timerCallbacks.front());
      m_timerCallbacks.pop_front();
      cb();
    }
    m_timer.StartOnce(1000);
  }

  void Terminate() { Close(true); }

  void PersistSettings() {
    if (!m_notebook)
      return;
    const wxString themeName = m_themeIsDark ? "dark" : "light";
    const wxFont font = m_persistedFont.IsOk()
                            ? m_persistedFont
                            : (m_view ? m_view->GetTheme().font : wxFont{});
    AppPersistence::Save(themeName, font, m_safeDrawingEnabled);
  }

  void ApplyNativeAppTheme(std::optional<bool> darkMode = std::nullopt) {
#if wxCHECK_VERSION(3, 3, 0)
    const bool enableDark = darkMode.value_or(m_themeIsDark);
    if (enableDark) {
      // force dark
      wxTheApp->SetAppearance(wxAppBase::Appearance::Dark);

    } else {
      wxTheApp->SetAppearance(wxAppBase::Appearance::Light);
    }
#else
    wxUnusedVar(darkMode);
#endif
  }

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

class MyApp : public wxApp {
public:
  enum class AppearanceSetting { Auto, Light, Dark };

  static AppearanceSetting ParseAppearanceSetting(const wxString &value) {
    const wxString lowered = value.Lower();
    if (lowered == "auto") {
      return AppearanceSetting::Auto;
    }
    if (lowered == "light") {
      return AppearanceSetting::Light;
    }
    if (lowered == "dark") {
      return AppearanceSetting::Dark;
    }

    throw std::invalid_argument("invalid appearance value");
  }

  void ApplyAppearanceSetting(AppearanceSetting appearance) {
#if wxCHECK_VERSION(3, 3, 0)
    switch (appearance) {
    case AppearanceSetting::Auto:
      SetAppearance(wxAppBase::Appearance::System);
      break;
    case AppearanceSetting::Light:
      SetAppearance(wxAppBase::Appearance::Light);
      break;
    case AppearanceSetting::Dark:
      SetAppearance(wxAppBase::Appearance::Dark);
      break;
    }
#else
    wxUnusedVar(appearance);
#endif
  }

  bool OnInit() override {
    static const wxCmdLineEntryDesc cmdLineDesc[] = {
        {wxCMD_LINE_SWITCH, "h", "help", "show help", wxCMD_LINE_VAL_NONE,
         wxCMD_LINE_OPTION_HELP},
        {wxCMD_LINE_OPTION, nullptr, "appearance",
         "native app appearance: light, dark, or auto", wxCMD_LINE_VAL_STRING,
         0},
        {wxCMD_LINE_OPTION, "l", "log-level",
         "set log level: trace, debug, warn, error", wxCMD_LINE_VAL_STRING, 0},
        {wxCMD_LINE_OPTION, "s", "shell",
         "shell command to launch instead of the default shell",
         wxCMD_LINE_VAL_STRING, 0},
        {wxCMD_LINE_OPTION, nullptr, "working-directory",
         "working directory for the launched shell", wxCMD_LINE_VAL_STRING, 0},
        {wxCMD_LINE_OPTION, nullptr, "command", "command to execute",
         wxCMD_LINE_VAL_STRING, 0},
        {wxCMD_LINE_OPTION, nullptr, "title", "set a title for the terminal",
         wxCMD_LINE_VAL_STRING, 0},
        {wxCMD_LINE_OPTION, nullptr, "env",
         "environment list (Windows: A=B;C=D, POSIX: A=B:C=D)",
         wxCMD_LINE_VAL_STRING, 0},
        {wxCMD_LINE_NONE}};

    wxCmdLineParser parser(cmdLineDesc, argc, argv);
    if (parser.Parse() != 0) {
      return false;
    }

    wxString logLevelStr;
    if (parser.Found("log-level", &logLevelStr)) {
      logLevelStr = logLevelStr.Lower();
      TerminalLogLevel level = TerminalLogLevel::kDebug;
      if (logLevelStr == "trace") {
        level = TerminalLogLevel::kTrace;
      } else if (logLevelStr == "debug") {
        level = TerminalLogLevel::kDebug;
      } else if (logLevelStr == "warn" || logLevelStr == "warning") {
        level = TerminalLogLevel::kWarn;
      } else if (logLevelStr == "error") {
        level = TerminalLogLevel::kError;
      } else if (logLevelStr == "info") {
        level = TerminalLogLevel::kInfo;
      } else {
        wxLogError("Unknown log level: %s", logLevelStr);
        return false;
      }
      TerminalLogger::Get().SetLevel(level);
    } else {
      TerminalLogger::Get().SetLevel(TerminalLogLevel::kError);
    }

    wxString envStr;
    std::optional<MyFrame::EnvironmentList> environment{std::nullopt};
    if (parser.Found("env", &envStr)) {
      environment = MyFrame::ParseEnvironmentList(envStr);
    }

    std::optional<wxString> workingDirectory{std::nullopt};
    wxString workingDirectoryStr;
    if (parser.Found("working-directory", &workingDirectoryStr)) {
      if (workingDirectoryStr.empty()) {
        wxLogError("--working-directory requires a non-empty path");
        return false;
      }
      if (!wxDirExists(workingDirectoryStr)) {
        wxLogError("Working directory does not exist: %s", workingDirectoryStr);
        return false;
      }
      workingDirectory = workingDirectoryStr;
    }

    AppearanceSetting appearanceSetting = AppearanceSetting::Auto;
    wxString appearanceStr;
    if (parser.Found("appearance", &appearanceStr)) {
      try {
        appearanceSetting = ParseAppearanceSetting(appearanceStr);
      } catch (const std::invalid_argument &) {
        wxLogError("Unknown appearance value: %s", appearanceStr);
        return false;
      }
    }

    ApplyAppearanceSetting(appearanceSetting);

    wxString command;
    if (parser.Found("command", &command)) {
      if (command.empty()) {
        wxLogError("--command requires a non-empty command");
        return false;
      }
    }

    wxString title;
    if (parser.Found("title", &title)) {
      if (title.empty()) {
        wxLogError("--title requires a non-empty string");
        return false;
      }
    }

    auto frame = new MyFrame(parser, environment, std::move(workingDirectory));
    frame->Show();
    SetTopWindow(frame);
    return true;
  }
};

wxIMPLEMENT_APP(MyApp);
