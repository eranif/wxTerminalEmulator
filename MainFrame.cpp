#include "MainFrame.h"
#include <wx/msgdlg.h>
#include <wx/xrc/xmlres.h>

MyFrame::MyFrame(const wxCmdLineParser &parser,
                 const std::optional<EnvironmentList> &environment,
                 std::optional<wxString> workingDirectory)
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

  wxDisplay display(wxDisplay::GetFromWindow(this));
  wxRect screen = display.GetClientArea();

  int width = screen.width / 2;
  int height = screen.height / 2;
  SetSize(width, height);
  CentreOnScreen();

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
  m_notebook->Bind(
      wxEVT_AUINOTEBOOK_TAB_RIGHT_UP,
      [this](wxAuiNotebookEvent &event) { CallAfter(&MyFrame::ShowTabMenu); });
  if (m_view && !command.empty()) {
    m_view->SendCommand(command);
  }
}

void MyFrame::ShowTabMenu() {
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

std::optional<EnvironmentList>
MyFrame::ParseEnvironmentList(const wxString &s) {
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

void MyFrame::BuildMenuBar() {
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

  auto *editMenu = new wxMenu();
  editMenu->Append(ID_CopyAll, _("Copy All Text"));
  menuBar->Append(editMenu, "Edit");

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
  Bind(wxEVT_MENU, &MyFrame::OnCopyAll, this, ID_CopyAll);
  Bind(wxEVT_UPDATE_UI, &MyFrame::OnNextTabUI, this, wxID_FORWARD);
  Bind(wxEVT_UPDATE_UI, &MyFrame::OnPreviousTabUI, this, wxID_BACKWARD);
}

void MyFrame::OnNextTabUI(wxUpdateUIEvent &event) {
  event.Enable(m_notebook->GetPageCount() > 1);
}

void MyFrame::OnPreviousTabUI(wxUpdateUIEvent &event) {
  event.Enable(m_notebook->GetPageCount() > 1);
}

void MyFrame::OnNextTab(wxCommandEvent &event) {
  wxUnusedVar(event);
  if (m_notebook->GetPageCount() == 0) {
    return;
  }
  int currentTabIndex = m_notebook->GetSelection();
  if (currentTabIndex == wxNOT_FOUND) {
    return;
  }

  if (currentTabIndex + 1 >= m_notebook->GetPageCount()) {
    currentTabIndex = 0;
  } else {
    currentTabIndex += 1;
  }
  m_notebook->SetSelection(currentTabIndex);
}

void MyFrame::OnPreviousTab(wxCommandEvent &event) {
  wxUnusedVar(event);
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

void MyFrame::OnCloseTab(wxCommandEvent &event) {
  wxUnusedVar(event);
  int currentTabIndex = m_notebook->GetSelection();
  if (currentTabIndex == wxNOT_FOUND) {
    return;
  }
  m_notebook->DeletePage(static_cast<size_t>(currentTabIndex));
}

void MyFrame::ApplyThemeToAllTabs(const wxTerminalTheme &theme) {
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

void MyFrame::ApplyFontToAllTabs(const wxFont &font) {
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

wxTerminalViewCtrl *MyFrame::GetActiveTerminalView() const {
  if (!m_notebook || m_notebook->GetPageCount() == 0) {
    return nullptr;
  }
  return dynamic_cast<wxTerminalViewCtrl *>(m_notebook->GetCurrentPage());
}

MyTerminal *MyFrame::CreateTerminalPage(const TerminalPageConfig &config) {
  auto *page = new MyTerminal(m_notebook, config.shellCommand,
                              config.environment, config.workingDirectory);
  m_notebook->AddPage(page, "Terminal", true);
  ApplyThemeToTab(page);
  m_safeDrawingEnabled =
      m_safeDrawingEnabled || wxTerminalViewCtrl::IsOpenGLEnabled();
  page->EnableSafeDrawing(m_safeDrawingEnabled);
  UpdateSafeDrawingMenuCheck();
  page->Bind(wxEVT_TERMINAL_TERMINATED, &MyFrame::OnTerminated, this);
  page->Bind(wxEVT_TERMINAL_TEXT_LINK, &MyFrame::OnTerminalLink, this);
  page->Bind(wxEVT_TERMINAL_BELL, &MyFrame::OnBell, this);
  return page;
}

void MyFrame::ApplySafeDrawingToAllTabs(bool enabled) {
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

void MyFrame::UpdateSafeDrawingMenuCheck() {
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

void MyFrame::ApplyThemeToTab(wxTerminalViewCtrl *view) {
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

void MyFrame::OnNewTerminal(wxCommandEvent &event) {
  wxUnusedVar(event);
  CreateTerminalPage(
      {m_defaultShellCommand, m_defaultEnvironment, m_defaultWorkingDirectory});
  m_notebook->SetSelection(m_notebook->GetPageCount() - 1);
}

void MyFrame::OnExit(wxCommandEvent &event) {
  wxUnusedVar(event);
  Close(true);
}

void MyFrame::OnDarkTheme(wxCommandEvent &event) {
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

void MyFrame::OnLightTheme(wxCommandEvent &event) {
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

void MyFrame::OnChangeFont(wxCommandEvent &event) {
  wxUnusedVar(event);
  wxTerminalViewCtrl *activeView = GetActiveTerminalView();
  if (!activeView) {
    return;
  }
  wxFontData fontData;
  fontData.EnableEffects(false);
  fontData.SetInitialFont(m_persistedFont.IsOk() ? m_persistedFont
                                                 : activeView->GetTheme().font);
  wxFontDialog dlg(this, fontData);
  if (dlg.ShowModal() != wxID_OK) {
    return;
  }
  m_persistedFont = dlg.GetFontData().GetChosenFont();
  ApplyFontToAllTabs(m_persistedFont);
  PersistSettings();
}

void MyFrame::OnCenterLine(wxCommandEvent &event) {
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

void MyFrame::OnSafeDrawing(wxCommandEvent &event) {
  m_safeDrawingEnabled = event.IsChecked();
  ApplySafeDrawingToAllTabs(m_safeDrawingEnabled);
  PersistSettings();
}

void MyFrame::OnSetSelection(wxCommandEvent &event) {
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

void MyFrame::OnPrintLine(wxCommandEvent &event) {
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

void MyFrame::OnSendInput(wxCommandEvent &event) {
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

void MyFrame::OnCopyAll(wxCommandEvent &event) {
  wxUnusedVar(event);
  wxTerminalViewCtrl *activeView = GetActiveTerminalView();
  if (!activeView) {
    return;
  }
  if (wxTheClipboard->Open()) {
    wxTheClipboard->SetData(new wxTextDataObject(activeView->GetText()));
    wxTheClipboard->Flush();
    wxTheClipboard->Close();
    if (GetStatusBar()) {
      GetStatusBar()->SetStatusText(_("Text Copied!"));
    }
  }
}

void MyFrame::OnFindText(wxCommandEvent &event) {
  wxUnusedVar(event);
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
    m_findDialog->Bind(wxEVT_FIND, &MyFrame::OnFindNext, this);
    m_findDialog->Bind(wxEVT_FIND_NEXT, &MyFrame::OnFindNext, this);
  }
  m_findDialog->Show();
}

void MyFrame::OnFindNext(wxFindDialogEvent &event) {
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

void MyFrame::OnTerminated(wxTerminalEvent &event) {
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

void MyFrame::OnTerminalLink(wxTerminalEvent &event) {
  event.Skip();
  wxString clickedText = event.GetClickedText();
  if (clickedText.StartsWith("http://") || clickedText.StartsWith("https://")) {
    ::wxLaunchDefaultBrowser(clickedText);
  } else if (clickedText.StartsWith("file://")) {
    ::wxLaunchDefaultApplication(clickedText);
  } else if (wxFileName::FileExists(clickedText)) {
    ::wxLaunchDefaultApplication(clickedText);
  }
}

void MyFrame::OnBell(wxTerminalEvent &event) {
  auto view = dynamic_cast<wxTerminalViewCtrl *>(event.GetEventObject());
  if (!view || !m_notebook) {
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

void MyFrame::OnTimer(wxTimerEvent &event) {
  wxUnusedVar(event);
  while (!m_timerCallbacks.empty()) {
    auto cb = std::move(m_timerCallbacks.front());
    m_timerCallbacks.pop_front();
    cb();
  }
  m_timer.StartOnce(1000);
}

void MyFrame::Terminate() { Close(true); }

void MyFrame::PersistSettings() {
  if (!m_notebook)
    return;
  const wxString themeName = m_themeIsDark ? "dark" : "light";
  const wxFont font = m_persistedFont.IsOk()
                          ? m_persistedFont
                          : (m_view ? m_view->GetTheme().font : wxFont{});
  AppPersistence::Save(themeName, font, m_safeDrawingEnabled);
}

void MyFrame::ApplyNativeAppTheme(std::optional<bool> darkMode) {
#if wxCHECK_VERSION(3, 3, 0)
  const bool enableDark = darkMode.value_or(m_themeIsDark);
  if (enableDark) {
    wxTheApp->SetAppearance(wxAppBase::Appearance::Dark);
  } else {
    wxTheApp->SetAppearance(wxAppBase::Appearance::Light);
  }
#else
  wxUnusedVar(darkMode);
#endif
}
