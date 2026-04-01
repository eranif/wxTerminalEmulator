#include "app_persistence.h"
#include "terminal_event.h"
#include "terminal_logger.h"
#include "terminal_view.h"
#include <optional>
#include <wx/app.h>
#include <wx/cmdline.h>
#include <wx/display.h>
#include <wx/fontdlg.h>
#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sysopt.h>
#include <wx/textdlg.h>
#include <wx/tokenzr.h>

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
    ID_Exit
  };

  using EnvironmentList = terminal::PtyBackend::EnvironmentList;

  struct TerminalPageConfig {
    wxString shellCommand;
    std::optional<EnvironmentList> environment;
  };

  MyFrame(const wxCmdLineParser &parser,
          const std::optional<EnvironmentList> &environment)
      : wxFrame(nullptr, wxID_ANY, "wxTerminalEmulator") {
    wxString shellCommand;
    parser.Found("shell", &shellCommand);

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

    ApplyNativeAppTheme();

    BuildMenuBar();
    m_notebook = new wxNotebook(this, wxID_ANY);
    m_defaultShellCommand = shellCommand;
    m_defaultEnvironment = environment;
    m_view = CreateTerminalPage({shellCommand, environment});
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
    fileMenu->Append(ID_NewTerminal, "New terminal");
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
    SetMenuBar(menuBar);

    Bind(wxEVT_MENU, &MyFrame::OnNewTerminal, this, ID_NewTerminal);
    Bind(wxEVT_MENU, &MyFrame::OnExit, this, ID_Exit);
    Bind(wxEVT_MENU, &MyFrame::OnDarkTheme, this, ID_ThemeDark);
    Bind(wxEVT_MENU, &MyFrame::OnLightTheme, this, ID_ThemeLight);
    Bind(wxEVT_MENU, &MyFrame::OnChangeFont, this, ID_ChangeFont);
    Bind(wxEVT_MENU, &MyFrame::OnCenterLine, this, ID_CenterLine);
    Bind(wxEVT_MENU, &MyFrame::OnSafeDrawing, this, ID_SafeDrawing);
    Bind(wxEVT_MENU, &MyFrame::OnSetSelection, this, ID_SetSelection);
    Bind(wxEVT_MENU, &MyFrame::OnPrintLine, this, ID_PrintLine);
    Bind(wxEVT_MENU, &MyFrame::OnSendInput, this, ID_SendInput);
  }

  void ApplyThemeToAllTabs(const wxTerminalTheme &theme) {
    if (!m_notebook) {
      return;
    }

    for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
      if (auto *view = dynamic_cast<TerminalView *>(m_notebook->GetPage(i))) {
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
      if (auto *view = dynamic_cast<TerminalView *>(m_notebook->GetPage(i))) {
        wxTerminalTheme theme = view->GetTheme();
        theme.font = font;
        view->SetTheme(theme);
        view->Refresh();
      }
    }
  }

  TerminalView *GetActiveTerminalView() const {
    if (!m_notebook || m_notebook->GetPageCount() == 0) {
      return nullptr;
    }
    return dynamic_cast<TerminalView *>(m_notebook->GetCurrentPage());
  }

  TerminalView *CreateTerminalPage(const TerminalPageConfig &config) {
    auto *page =
        new TerminalView(m_notebook, config.shellCommand, config.environment);
    m_notebook->AddPage(page, "Terminal", true);
    ApplyThemeToTab(page);
    page->EnableSafeDrawing(m_safeDrawingEnabled);
    UpdateSafeDrawingMenuCheck();
    page->Bind(wxEVT_TERMINAL_TITLE_CHANGED, &MyFrame::OnTitleChanged, this);
    page->Bind(wxEVT_TERMINAL_TERMINATED, &MyFrame::OnTerminated, this);
    return page;
  }

  void ApplySafeDrawingToAllTabs(bool enabled) {
    if (!m_notebook) {
      return;
    }

    for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
      if (auto *view = dynamic_cast<TerminalView *>(m_notebook->GetPage(i))) {
        view->EnableSafeDrawing(enabled);
        view->Refresh();
      }
    }
  }

  void UpdateSafeDrawingMenuCheck() {
    if (auto *menuBar = GetMenuBar()) {
      auto *optionsMenu = menuBar->GetMenu(1);
      if (optionsMenu && optionsMenu->FindItem(ID_SafeDrawing)) {
        optionsMenu->Check(ID_SafeDrawing, m_safeDrawingEnabled);
      }
    }
  }

  void ApplyThemeToTab(TerminalView *view) {
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
    CreateTerminalPage({m_defaultShellCommand, m_defaultEnvironment});
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
    TerminalView *activeView = GetActiveTerminalView();
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
    TerminalView *activeView = GetActiveTerminalView();
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
    TerminalView *activeView = GetActiveTerminalView();
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

    activeView->SetUserSelection(static_cast<std::size_t>(colStart - 1),
                                 static_cast<std::size_t>(lineNumber - 1),
                                 static_cast<std::size_t>(count));
  }

  void OnPrintLine(wxCommandEvent &event) {
    wxUnusedVar(event);
    TerminalView *activeView = GetActiveTerminalView();
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
    TerminalView *activeView = GetActiveTerminalView();
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

  void OnTerminated(wxTerminalEvent &event) {
    wxUnusedVar(event);
    auto *view = dynamic_cast<TerminalView *>(event.GetEventObject());
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

    CallAfter([this, tabIndex]() {
      if (!m_notebook) {
        return;
      }

      if (tabIndex >= 0 &&
          tabIndex < static_cast<int>(m_notebook->GetPageCount())) {
        m_notebook->DeletePage(static_cast<size_t>(tabIndex));
      }

      if (m_notebook->GetPageCount() == 0) {
        Close(true);
      }
    });
  }
  void OnTitleChanged(wxTerminalEvent &event) {
    if (auto *view = dynamic_cast<TerminalView *>(event.GetEventObject())) {
      if (m_notebook) {
        for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
          if (m_notebook->GetPage(i) == view) {
            m_notebook->SetPageText(i, event.GetTitle());
            break;
          }
        }
      }
    }
    SetTitle(event.GetTitle());
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
#if defined(__WXMSW__) && wxCHECK_VERSION(3, 3, 0)
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
  wxNotebook *m_notebook{nullptr};
  TerminalView *m_view{nullptr};
  wxString m_defaultShellCommand;
  std::optional<EnvironmentList> m_defaultEnvironment;
  bool m_themeIsDark{true};
  wxFont m_persistedFont;
  bool m_safeDrawingEnabled{false};
};

class MyApp : public wxApp {
public:
  bool OnInit() override {
    static const wxCmdLineEntryDesc cmdLineDesc[] = {
        {wxCMD_LINE_SWITCH, "h", "help", "show help", wxCMD_LINE_VAL_NONE,
         wxCMD_LINE_OPTION_HELP},
        {wxCMD_LINE_OPTION, "l", "log-level",
         "set log level: trace, debug, warn, error", wxCMD_LINE_VAL_STRING, 0},
        {wxCMD_LINE_OPTION, "s", "shell",
         "shell command to launch instead of the default shell",
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

#ifdef __WXMSW__
    SetAppearance(wxAppBase::Appearance::System);
#endif

    auto frame = new MyFrame(parser, environment);
    frame->Show();
    SetTopWindow(frame);
    return true;
  }
};

wxIMPLEMENT_APP(MyApp);
