#include "MainFrame.h"
#include "GlyphBook.h"
#include "SettingsDlg.hpp"
#include <algorithm>
#include <random>
#include <wx/choicdlg.h>
#if wxCHECK_VERSION(3, 3, 0)
#include <wx/aui/serializer.h>
#endif
#include <wx/image.h>
#include <wx/msgdlg.h>
#include <wx/notifmsg.h>
#include <wx/xrc/xmlres.h>

namespace {
bool IsDark(const wxColour &c) {
  // Perceived luminance (ITU-R BT.601)
  double luminance = 0.299 * c.Red() + 0.587 * c.Green() + 0.114 * c.Blue();
  return luminance < 128.0;
}

// Random header colours: a random background, and white text on a dark
// background or black text on a light one, so the text is always readable.
void RandomHeaderColours(wxColour &fg, wxColour &bg) {
  static std::mt19937 rng{std::random_device{}()};
  std::uniform_real_distribution<double> hue(0.0, 1.0);
  std::uniform_real_distribution<double> saturation(0.4, 0.8);
  std::uniform_real_distribution<double> value(0.25, 1.0);
  const auto rgb = wxImage::HSVtoRGB(
      wxImage::HSVValue(hue(rng), saturation(rng), value(rng)));
  bg = wxColour(rgb.red, rgb.green, rgb.blue);
  fg = IsDark(bg) ? *wxWHITE : *wxBLACK;
}

// Status bar fields: 0 for messages, 1 for the directory of the active tab, 2
// for the text the program in the active tab copied with OSC 52.
constexpr int kStatusFieldDirectory = 1;
constexpr int kStatusFieldAppSelection = 2;
constexpr int kStatusFieldProgress = 3;
constexpr int kStatusFieldCount = 4;

// Text from a program is not trusted: make it a short single line.
wxString ToStatusBarText(const wxString &text, size_t maxLen) {
  wxString result;
  result.reserve(text.length());
  for (const wxUniChar ch : text) {
    const auto value = ch.GetValue();
    if (value == '\n') {
      result += wxT("↵");
    } else if (value == '\r') {
      continue;
    } else if (value < 0x20 || (value >= 0x7f && value < 0xa0)) {
      result += ' ';
    } else {
      result += ch;
    }
  }
  if (result.length() > maxLen) {
    result = result.Left(maxLen);
    // Do not leave half a surrogate pair where wxString is UTF-16.
    const auto last = result.Last().GetValue();
    if (last >= 0xD800 && last <= 0xDBFF) {
      result.RemoveLast();
    }
    result += wxT("...");
  }
  return result;
}

// Opens a file or a directory that the user clicked in the terminal. A
// relative path is resolved against the working directory of the terminal, wd
// (the directory of GlyphT when wd is empty), and a leading "~" is expanded.
bool TryOpenPath(const wxString &text, const wxString &wd) {
  wxFileName path{text};
  path.Normalize(wxPATH_NORM_TILDE | wxPATH_NORM_ABSOLUTE | wxPATH_NORM_DOTS,
                 wd);
  if (!path.Exists()) {
    return false;
  }
  ::wxLaunchDefaultApplication(path.GetFullPath());
  return true;
}

// The window style flag of the book for the tab position of the settings: the
// tabs are above or below the terminal.
long TabPositionFlag(TabPosition position) {
  return position == TabPosition::Bottom ? wxAUI_NB_BOTTOM : wxAUI_NB_TOP;
}

constexpr long kTabPositionFlags = wxAUI_NB_TOP | wxAUI_NB_BOTTOM;

#if wxCHECK_VERSION(3, 3, 0)
// The look of the tabs of the settings, as the art of the notebook.
GlyphTabPainter::Style TabArtStyle(TabStyle style) {
  return style == TabStyle::Pills ? GlyphTabPainter::Style::Pills
                                  : GlyphTabPainter::Style::Browser;
}

// The name of the notebook for wxAuiNotebook::SaveLayout() and LoadLayout().
const wxString kNotebookLayoutName = wxS("tabs");

// Takes the layout of the tab controls of the notebook (the tabs can be split
// and docked in it).
class TabLayoutSaver : public wxAuiBookSerializer {
public:
  void BeforeSaveNotebook(const wxString &) override {}
  void SaveNotebookTabControl(const wxAuiTabLayoutInfo &tab) override {
    LayoutPersistence::TabControlInfo info;
    info.dockDirection = tab.dock_direction;
    info.dockLayer = tab.dock_layer;
    info.dockRow = tab.dock_row;
    info.dockPos = tab.dock_pos;
    info.dockProportion = tab.dock_proportion;
    info.dockSize = tab.dock_size;
    info.pages = tab.pages;
    info.pinned = tab.pinned;
    info.active = tab.active;
    controls.push_back(std::move(info));
  }

  std::vector<LayoutPersistence::TabControlInfo> controls;
};

// Gives the saved layout of the tab controls back to the notebook.
class TabLayoutLoader : public wxAuiBookDeserializer {
public:
  explicit TabLayoutLoader(
      const std::vector<LayoutPersistence::TabControlInfo> &controls)
      : m_controls(controls) {}

  std::vector<wxAuiTabLayoutInfo> LoadNotebookTabs(const wxString &) override {
    std::vector<wxAuiTabLayoutInfo> result;
    for (const auto &control : m_controls) {
      wxAuiTabLayoutInfo tab;
      tab.dock_direction = control.dockDirection;
      tab.dock_layer = control.dockLayer;
      tab.dock_row = control.dockRow;
      tab.dock_pos = control.dockPos;
      tab.dock_proportion = control.dockProportion;
      tab.dock_size = control.dockSize;
      tab.pages = control.pages;
      tab.pinned = control.pinned;
      tab.active = control.active;
      result.push_back(std::move(tab));
    }
    return result;
  }

private:
  const std::vector<LayoutPersistence::TabControlInfo> &m_controls;
};
#endif

} // namespace

MyFrame::MyFrame(const wxCmdLineParser &parser,
                 const std::optional<EnvironmentList> &environment,
                 std::optional<wxString> workingDirectory)
#if USE_OPENGL
    : wxFrame(nullptr, wxID_ANY, "GlyphT (OpenGL)") {
#else
    : wxFrame(nullptr, wxID_ANY, "GlyphT") {
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

  wxString appearance;
  if (parser.Found("appearance", &appearance)) {
    appearance.MakeLower();
    if (appearance == "dark" || appearance == "light") {
      m_cliForceDark = appearance == "dark";
    }
  }

  AppPersistence::Load(m_config);
  ApplyNativeAppTheme();

  CreateStatusBar(kStatusFieldCount);
  const int statusWidths[kStatusFieldCount] = {-1, -2, -2, -2};
  SetStatusWidths(kStatusFieldCount, statusWidths);
  BuildMenuBar();
  long nbStyle = wxAUI_NB_DEFAULT_STYLE &
                 ~(wxAUI_NB_CLOSE_ON_ACTIVE_TAB | wxAUI_NB_CLOSE_ON_ALL_TABS |
                   wxAUI_NB_TAB_FIXED_WIDTH | kTabPositionFlags);
  nbStyle |= TabPositionFlag(m_config.GetTabPosition());
  if (m_config.GetShowCloseButton()) {
    nbStyle |= wxAUI_NB_CLOSE_ON_ACTIVE_TAB | wxAUI_NB_CLOSE_ON_ALL_TABS;
  }

  m_notebook =
      new TabBook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, nbStyle);
  ApplyTabArt(m_config.GetActiveTheme(), m_config.GetTabOutlineColour());
  // The bar of the tabs is the button face, which changes with the appearance
  // of the system.
  Bind(wxEVT_SYS_COLOUR_CHANGED, [this](wxSysColourChangedEvent &event) {
    event.Skip();
    ApplyTabArt(m_tabTheme, m_tabOutlineColour);
  });
  m_cliShellCommand = shellCommand;
  m_defaultShellCommand = ResolveDefaultShell();
  m_defaultEnvironment = environment;
  m_defaultWorkingDirectory = std::move(workingDirectory);

  // Bound before the tabs are created or restored, so the tab that is selected
  // by RestoreLayout() is handled like any other change of the current tab.
  m_notebook->Bind(
      wxEVT_AUINOTEBOOK_PAGE_CHANGED, [this](wxAuiNotebookEvent &event) {
        event.Skip();
        if (m_notebook->GetPageCount() == 0) {
          return;
        }
        auto page = m_notebook->GetPage(m_notebook->GetSelection());
        if (page) {
          m_tabMru.Touch(page);
          page->SetFocus();
        }
        UpdateDirectoryStatus();
        UpdateProgressStatus();
        UpdateAppSelectionStatus();
      });
  // The notebook shows no menu: the application builds it.
  m_notebook->Bind(wxEVT_AUINOTEBOOK_TAB_RIGHT_UP,
                   [this](wxAuiNotebookEvent &event) {
                     CallAfter(&MyFrame::ShowTabMenu, event.GetSelection(),
                               wxDefaultPosition);
                   });
  // A double click on the bar, like on a browser, opens a new tab.
  m_notebook->Bind(wxEVT_AUINOTEBOOK_BG_DCLICK, [this](wxAuiNotebookEvent &) {
    CallAfter([this] {
      wxCommandEvent event;
      OnNewTerminal(event);
    });
  });

  // When the user launches with a command or a working directory, honour those
  // and start a single fresh terminal. Otherwise try to restore the previously
  // saved layout. --shell does not stop the restore: it only chooses the shell
  // of the restored tabs (see RestoreLayout()).
  const bool hasExplicitLaunch =
      !command.empty() || m_defaultWorkingDirectory.has_value();
  bool restored = false;
  if (!hasExplicitLaunch) {
    restored = RestoreLayout();
  }

  if (!restored) {
    CreateTerminalPage(
        {m_defaultShellCommand, environment, m_defaultWorkingDirectory}, true);
  }

  Bind(wxEVT_CLOSE_WINDOW, &MyFrame::OnClose, this);
  if (GetActiveTerminalView() && !command.empty()) {
    GetActiveTerminalView()->SendCommand(command);
  }
}

MyFrame::~MyFrame() {}

void MyFrame::OnClose(wxCloseEvent &event) {
  SaveLayout();
  PersistSettings();
  event.Skip();
}

void MyFrame::SaveLayout() {
  if (!m_notebook) {
    return;
  }

  // Collect per-tab metadata in page order. The notebook describes its tab
  // controls with the indexes of the pages, so a page that is not saved would
  // shift the others. Then the arrangement is not saved (only the order of the
  // tabs and the current one are), and the tabs come back in one tab control.
  const int selection = m_notebook->GetSelection();
  bool allSaved = true;
  std::vector<LayoutPersistence::TabInfo> tabs;
  for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
    auto *terminal = TerminalOf(m_notebook->GetPage(i));
    if (!terminal) {
      allSaved = false;
      continue;
    }
    LayoutPersistence::TabInfo info;
    info.shellCommand = terminal->GetShellCommand();
    // Prefer the directory the shell reported last. It is a local path only if
    // the shell is local, so a directory that does not exist here (for example
    // one on a remote host) is not saved.
    info.workingDirectory = terminal->GetCurrentDir();
    if (info.workingDirectory.empty() || !wxDirExists(info.workingDirectory)) {
      info.workingDirectory = terminal->GetWorkingDirectory();
      if (!wxDirExists(info.workingDirectory)) {
        info.workingDirectory.clear();
      }
    }
    info.customLabel = terminal->GetCustomLabel();
    info.selected = static_cast<int>(i) == selection;
    tabs.push_back(std::move(info));
  }

  if (tabs.empty()) {
    LayoutPersistence::Clear();
    return;
  }

  // How the tabs are arranged: split, docked, reordered.
  std::vector<LayoutPersistence::TabControlInfo> tabControls;
#if wxCHECK_VERSION(3, 3, 0)
  if (allSaved) {
    TabLayoutSaver saver;
    m_notebook->SaveLayout(kNotebookLayoutName, saver);
    tabControls = std::move(saver.controls);
  }
#endif
  SaveLayoutFile(tabs, tabControls, GetSize());
}

bool MyFrame::RestoreLayout() {
  std::optional<LayoutFile> layout = LoadLayoutFile();
  if (!layout || layout->IsEmpty()) {
    return false;
  }

  if (layout->frameSize) {
    // Keep the window inside the visible area of the screen.
    const wxRect screen =
        wxDisplay(wxDisplay::GetFromWindow(this)).GetClientArea();
    SetSize(wxSize(std::min(layout->frameSize->x, screen.width),
                   std::min(layout->frameSize->y, screen.height)));
    CentreOnScreen();
  }

  // The tabs are created in the order of the file, so the page indexes of the
  // tab controls are the indexes of the tabs.
  int selected = wxNOT_FOUND;
  for (const auto &tab : layout->tabs) {
    std::optional<wxString> cwd;
    if (!tab.workingDirectory.empty() && wxDirExists(tab.workingDirectory)) {
      cwd = tab.workingDirectory;
    }
    // A shell given on the command line (--shell) wins over the saved one. The
    // shell of the settings does not: each tab keeps the shell it was started
    // with (for example a WSL distribution). It is only the fallback for tabs
    // saved without a shell.
    wxString shell = m_cliShellCommand;
    if (shell.empty()) {
      shell =
          tab.shellCommand.empty() ? m_defaultShellCommand : tab.shellCommand;
    }
    MyTerminal *page =
        CreateTerminalPage({shell, m_defaultEnvironment, cwd}, false);
    if (!page) {
      continue;
    }
    if (!tab.customLabel.empty()) {
      page->SetTabLabel(tab.customLabel);
    }
    if (tab.selected && selected == wxNOT_FOUND) {
      selected = static_cast<int>(m_notebook->GetPageCount()) - 1;
    }
  }

  for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
    m_notebook->GetPage(i)->SetFocus();
  }

#if wxCHECK_VERSION(3, 3, 0)
  // Split and dock the tabs like they were. This is only right when every tab
  // was created, as the file refers to the tabs by their index, and when the
  // file is consistent (it can be edited by hand): a bad layout would leave a
  // page in no tab control, which makes it invisible.
  if (m_notebook->GetPageCount() == layout->tabs.size() &&
      AreTabControlsValid(layout->tabControls, layout->tabs.size())) {
    TabLayoutLoader loader(layout->tabControls);
    m_notebook->LoadLayout(kNotebookLayoutName, loader);
  }
#endif

  // A file of an older version has the title of the current tab and no mark.
  // The first tab with that title is the current one.
  if (selected == wxNOT_FOUND && !layout->activeTitle.empty()) {
    for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
      if (m_notebook->GetPageText(i) == layout->activeTitle) {
        selected = static_cast<int>(i);
        break;
      }
    }
  }
  if (selected != wxNOT_FOUND) {
    m_notebook->SetSelection(static_cast<size_t>(selected));
  }
  return m_notebook->GetPageCount() > 0;
}

void MyFrame::ShowTabMenu(int tabIdx, const wxPoint &pos) {
  if (tabIdx == wxNOT_FOUND) {
    tabIdx =
        m_notebook->HitTest(m_notebook->ScreenToClient(::wxGetMousePosition()));
  }
  auto *clickedTerminal =
      tabIdx == wxNOT_FOUND
          ? nullptr
          : TerminalOf(m_notebook->GetPage(static_cast<size_t>(tabIdx)));

  wxMenu menu;
  menu.Append(XRCID("rename-tab"), _("Set Label..."));
  menu.Append(wxID_CLEAR, _("Reset Label..."));
  menu.Enable(XRCID("rename-tab"), clickedTerminal != nullptr);
  menu.Enable(wxID_CLEAR, clickedTerminal != nullptr);
#if wxCHECK_VERSION(3, 3, 0)
  // Own colours for the tab, for example to mark it. The menu is shown with
  // PopupMenu(), so the panel exists as long as the handlers can run.
  auto *panel = tabIdx == wxNOT_FOUND
                    ? nullptr
                    : dynamic_cast<TerminalPanel *>(
                          m_notebook->GetPage(static_cast<size_t>(tabIdx)));
  auto *tabColours = new wxMenu;
  const auto setTabColours = [this,
                              panel](const std::optional<GlyphColourPair> &c) {
    panel->SetTabColours(c);
    m_notebook->Refresh();
  };
  wxMenuItem *noColour = tabColours->Append(wxID_ANY, _("None"));
  tabColours->Bind(
      wxEVT_MENU,
      [setTabColours](wxCommandEvent &) { setTabColours(std::nullopt); },
      noColour->GetId());
  tabColours->AppendSeparator();
  for (const auto &entry : GlyphBook::GetGroupColours()) {
    wxMenuItem *item =
        tabColours->Append(wxID_ANY, wxGetTranslation(entry.name));
    const wxColour bg = entry.colour;
    tabColours->Bind(
        wxEVT_MENU,
        [setTabColours, bg](wxCommandEvent &) {
          setTabColours(GlyphColourPair{bg, IsDark(bg) ? *wxWHITE : *wxBLACK});
        },
        item->GetId());
  }
  menu.AppendSubMenu(tabColours, _("Colour Tab"))->Enable(panel != nullptr);
#endif
  menu.AppendSeparator();
  // The header and the footer have the same two items.
  for (const bool footer : {false, true}) {
    const int setId = XRCID(footer ? "set-footer" : "set-header");
    const int removeId = XRCID(footer ? "remove-footer" : "remove-header");
    menu.Append(setId, footer ? _("Set Footer...") : _("Set Header..."));
    menu.Append(removeId, footer ? _("Remove Footer") : _("Remove Header"));
    menu.Enable(setId, clickedTerminal != nullptr);
    menu.Enable(removeId, clickedTerminal != nullptr &&
                              (footer ? clickedTerminal->HasFooter()
                                      : clickedTerminal->HasHeader()));
    menu.Bind(
        wxEVT_MENU,
        [this, tabIdx, footer](wxCommandEvent &) {
          if (tabIdx != wxNOT_FOUND) {
            SetTabStrip(static_cast<size_t>(tabIdx), footer);
          }
        },
        setId);
    menu.Bind(
        wxEVT_MENU,
        [clickedTerminal, footer](wxCommandEvent &) {
          if (!clickedTerminal) {
            return;
          }
          if (footer) {
            clickedTerminal->RemoveFooter();
          } else {
            clickedTerminal->RemoveHeader();
          }
        },
        removeId);
  }
  menu.Bind(
      wxEVT_MENU,
      [clickedTerminal](wxCommandEvent &) {
        if (clickedTerminal == nullptr) {
          return;
        }
        wxString name = ::wxGetTextFromUser(_("New Tab Name:"));
        if (name.empty()) {
          return;
        }
        clickedTerminal->SetTabLabel(name);
      },
      XRCID("rename-tab"));
  menu.Bind(
      wxEVT_MENU,
      [clickedTerminal](wxCommandEvent &) {
        if (clickedTerminal != nullptr) {
          clickedTerminal->ResetTabLabel();
        }
      },
      wxID_CLEAR);

  m_notebook->PopupMenu(&menu, pos);
}

void MyFrame::SetTabStrip(size_t tabIdx, bool footer) {
  auto *terminal = TerminalOf(m_notebook->GetPage(tabIdx));
  if (terminal == nullptr) {
    return;
  }
  const wxString text = ::wxGetTextFromUser(
      footer ? _("Footer text:") : _("Header text:"),
      footer ? _("Set Footer") : _("Set Header"),
      footer ? terminal->GetFooterText() : terminal->GetHeaderText(), this);
  if (text.empty()) {
    return;
  }
  wxColour fg, bg;
  RandomHeaderColours(fg, bg);
  if (footer) {
    terminal->AddFooter(text, fg, bg);
  } else {
    terminal->AddHeader(text, fg, bg);
  }
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
  fileMenu->Append(wxID_NEW, _("New Terminal\tCtrl-T"));
  fileMenu->Append(wxID_CLOSE, _("Close Terminal"));
  fileMenu->AppendSeparator();
  fileMenu->Append(wxID_PREFERENCES);
  fileMenu->AppendSeparator();
  fileMenu->Append(ID_Exit, "Exit");
  menuBar->Append(fileMenu, "File");

  auto *optionsMenu = new wxMenu();
  optionsMenu->Append(ID_CenterLine, "Center Line...");
  optionsMenu->Append(ID_SetSelection, "Set Selection...");
  optionsMenu->Append(ID_PrintLine, "Print Line...");
  optionsMenu->Append(ID_SendInput, "Send Input...");
  menuBar->Append(optionsMenu, "Options");

  auto *searchMenu = new wxMenu();
  searchMenu->Append(wxID_FORWARD, _("Next Tab\tCtrl-RIGHT"));
  searchMenu->Append(wxID_BACKWARD, _("Previous Tab\tCtrl-LEFT"));
#ifdef __WXMAC__
  // macOS takes Ctrl+Tab before the app sees a key event, but it does
  // deliver menu shortcuts.
  searchMenu->Append(ID_TabSwitcherNext, _("Switch Tab\tRawCtrl-TAB"));
  searchMenu->Append(ID_TabSwitcherPrev,
                     _("Switch Tab Backwards\tRawCtrl-Shift-TAB"));
  Bind(
      wxEVT_MENU, [this](wxCommandEvent &) { ShowTabSwitcher(false); },
      ID_TabSwitcherNext);
  Bind(
      wxEVT_MENU, [this](wxCommandEvent &) { ShowTabSwitcher(true); },
      ID_TabSwitcherPrev);
#endif
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
  Bind(wxEVT_MENU, &MyFrame::OnCenterLine, this, ID_CenterLine);
  Bind(wxEVT_MENU, &MyFrame::OnSetSelection, this, ID_SetSelection);
  Bind(wxEVT_MENU, &MyFrame::OnPrintLine, this, ID_PrintLine);
  Bind(wxEVT_MENU, &MyFrame::OnSendInput, this, ID_SendInput);
  Bind(wxEVT_MENU, &MyFrame::OnFindText, this, ID_FindText);
  Bind(wxEVT_MENU, &MyFrame::OnCopyAll, this, ID_CopyAll);
  Bind(wxEVT_MENU, &MyFrame::OnSettings, this, wxID_PREFERENCES);
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

bool MyFrame::ShowTabSwitcher(bool backwards) {
  if (m_notebook->GetPageCount() < 2) {
    return false;
  }
  if (m_tabSwitcher) {
    m_tabSwitcher->Advance(backwards ? -1 : 1);
    return true;
  }

  TabMRU::Pages existing;
  for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
    existing.push_back(m_notebook->GetPage(i));
  }
  m_tabMru.Sync(existing, m_notebook->GetCurrentPage());

  std::vector<TabSwitcherDlg::Item> items;
  for (wxWindow *page : m_tabMru.GetPages()) {
    TabSwitcherDlg::Item item;
    item.label = m_notebook->GetPageText(
        static_cast<size_t>(m_notebook->FindPage(page)));
    if (auto *view = TerminalOf(page)) {
      item.directory = view->GetCurrentDir();
    }
    item.page = page;
    items.push_back(std::move(item));
  }

  m_tabSwitcher = new TabSwitcherDlg(
      this, std::move(items), backwards,
      [this](wxWindow *chosen, bool lostFocus) {
        m_config.SetTabSwitcherSize(m_tabSwitcher->GetSavedSize());
        m_tabSwitcher = nullptr;
        // The tab may have been closed while the dialog was open.
        const int idx = chosen ? m_notebook->FindPage(chosen) : wxNOT_FOUND;
        if (idx != wxNOT_FOUND) {
          m_notebook->SetSelection(static_cast<size_t>(idx));
        }
        // Do not take the focus back if the user switched to another app.
        if (!lostFocus) {
          if (auto *current = m_notebook->GetCurrentPage()) {
            current->SetFocus();
          }
        }
      },
      m_config.GetTabSwitcherSize());
  m_tabSwitcher->ShowPopup();
  return true;
}

void MyFrame::OnCloseTab(wxCommandEvent &event) {
  wxUnusedVar(event);
  int currentTabIndex = m_notebook->GetSelection();
  if (currentTabIndex == wxNOT_FOUND) {
    return;
  }
  m_notebook->DeletePage(static_cast<size_t>(currentTabIndex));
}

void MyFrame::ApplySettings() {
  if (!m_notebook) {
    return;
  }

  // Theme and font
  auto theme = m_config.GetActiveTheme();
  theme.isBlockCursor = m_config.GetBlockCursor();
  if (m_config.GetFont().IsOk()) {
    theme.font = m_config.GetFont();
  }
  // The app appearance first: the bar of the tabs uses the button face, which
  // depends on the appearance.
  ApplyNativeAppTheme();
  ApplyThemeToAllTabs(theme, m_config.GetTabOutlineColour());
  for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
    if (auto *view = TerminalOf(m_notebook->GetPage(i))) {
      view->EnableSafeDrawing(m_config.IsSafeDrawingEnabled());
    }
  }

  // Tab close button and tab position
  long style = m_notebook->GetWindowStyleFlag();
  style &= ~(wxAUI_NB_CLOSE_ON_ACTIVE_TAB | wxAUI_NB_CLOSE_ON_ALL_TABS |
             kTabPositionFlags);
  style |= TabPositionFlag(m_config.GetTabPosition());
  if (m_config.GetShowCloseButton()) {
    style |= wxAUI_NB_CLOSE_ON_ACTIVE_TAB | wxAUI_NB_CLOSE_ON_ALL_TABS;
  }
  m_notebook->SetWindowStyleFlag(style);
  m_notebook->Refresh();
}

void MyFrame::ApplyThemeToAllTabs(const wxTerminalTheme &theme,
                                  const wxString &outlineColour) {
  if (!m_notebook) {
    return;
  }
  for (size_t i = 0; i < m_notebook->GetPageCount(); ++i) {
    if (auto *view = TerminalOf(m_notebook->GetPage(i))) {
      view->SetTheme(theme);
      view->Refresh();
    }
  }
  // The tabs use the colours of the theme.
  ApplyTabArt(theme, outlineColour);
  m_notebook->Refresh();
}

void MyFrame::ApplyTabArt(const wxTerminalTheme &theme,
                          const wxString &outlineColour) {
  m_tabTheme = theme;
  m_tabOutlineColour = outlineColour;
#if wxCHECK_VERSION(3, 3, 0)
  auto *art = new GlyphTabArt(theme, TabArtStyle(m_config.GetTabStyle()));
  art->SetOutlineColour(AppConfig::GetTabOutlineColourOf(theme, outlineColour));
  // The bar is the button face, like the rest of the window.
  art->SetBarColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
  // Room for the status icon (see TabStatusIcon), so a tab does not grow when
  // it appears.
  art->SetReservedIconSize(16);
  // A tab can have its own colours (see ShowTabMenu()).
  art->SetColourProvider([](wxWindow *page) -> std::optional<GlyphColourPair> {
    auto *panel = dynamic_cast<TerminalPanel *>(page);
    return panel ? panel->GetTabColours() : std::nullopt;
  });
  // Taller tabs than the default of the platform: 3 DIPs more above and below
  // the label. The default is 6 DIPs, and 10 DIPs on GTK (see kPaddingY in
  // GlyphTabPainter.cpp).
#ifdef __WXGTK__
  art->SetPaddingY(13);
#else
  art->SetPaddingY(9);
#endif
  m_notebook->SetArtProvider(art);
#endif
}

wxTerminalViewCtrl *MyFrame::GetActiveTerminalView() const {
  if (!m_notebook || m_notebook->GetPageCount() == 0) {
    return nullptr;
  }
  return TerminalOf(m_notebook->GetCurrentPage());
}

MyTerminal *MyFrame::TerminalOf(wxWindow *page) {
  auto *panel = dynamic_cast<TerminalPanel *>(page);
  return panel ? panel->GetTerminal() : nullptr;
}

MyTerminal *MyFrame::CreateTerminalPage(const TerminalPageConfig &config,
                                        bool selectIt) {
  // The page of the notebook is the panel, the terminal is inside it.
  auto *panel = new TerminalPanel(m_notebook, config.shellCommand,
                                  config.environment, config.workingDirectory);
  MyTerminal *terminal = panel->GetTerminal();
  m_notebook->AddPage(panel, m_config.GetNewTabTitle(), selectIt);
  m_tabMru.AddPage(panel, selectIt);
  ApplyThemeToTab(terminal);
  m_config.SetSafeDrawingEnabled(m_config.IsSafeDrawingEnabled() ||
                                 wxTerminalViewCtrl::IsOpenGLEnabled());
  terminal->EnableSafeDrawing(m_config.IsSafeDrawingEnabled());
  // With panes side by side, a line in the blue of the theme shows which
  // terminal gets the keys.
  terminal->SetFocusedEffect(wxTerminalViewCtrl::kRightLine);
  terminal->RegisterKeyEventFilter([this](const wxKeyEvent &key) {
    if (key.GetKeyCode() != WXK_TAB || !key.RawControlDown() || key.AltDown()) {
      return false;
    }
    return ShowTabSwitcher(key.ShiftDown());
  });

  // A Ctrl/Cmd+CLICK on a word opens it as a link, even if the TUI app tracks
  // the mouse. Blank cells are not offered, so the app still gets those.
  terminal->RegisterTextClickedFilter(
      [](const std::string &) -> bool { return true; });

  terminal->Bind(wxEVT_TERMINAL_TERMINATED, &MyFrame::OnTerminated, this);
  terminal->Bind(wxEVT_TERMINAL_TEXT_LINK, &MyFrame::OnTerminalLink, this);
  terminal->Bind(wxEVT_TERMINAL_BELL, &MyFrame::OnBell, this);
  terminal->Bind(wxEVT_TERMINAL_CWD_CHANGED, &MyFrame::OnCwdChanged, this);
  terminal->SetProgressCallback([this](MyTerminal *source) {
    // Tabs in the background must not change the active tab's bar.
    if (source == GetActiveTerminalView()) {
      UpdateProgressStatus();
    }
  });
  terminal->Bind(wxEVT_TERMINAL_APP_SELECTION_CHANGED,
                 &MyFrame::OnAppSelectionChanged, this);
  terminal->Bind(wxEVT_TERMINAL_NOTIFICATION, &MyFrame::OnNotification, this);
  terminal->EnsureStarted();
  return terminal;
}

void MyFrame::ApplyThemeToTab(wxTerminalViewCtrl *view) {
  if (!view) {
    return;
  }
  auto theme = m_config.GetActiveTheme();
  theme.isBlockCursor = m_config.GetBlockCursor();
  // Without a saved font, keep the default font of the theme.
  if (m_config.GetFont().IsOk()) {
    theme.font = m_config.GetFont();
  }
  view->SetTheme(theme);
}

void MyFrame::OnNewTerminal(wxCommandEvent &event) {
  wxUnusedVar(event);
  const auto shell = PromptForShell();
  if (!shell) {
    return;
  }
  CreateTerminalPage({*shell, m_defaultEnvironment, m_defaultWorkingDirectory},
                     true);
  m_notebook->SetSelection(m_notebook->GetPageCount() - 1);
}

std::optional<wxString> MyFrame::PromptForShell() {
#ifdef __WXMSW__
  if (!m_shellOptions) {
    wxBusyCursor busy;
    m_shellOptions = LocateShells();
  }
  std::vector<ShellOption> options = *m_shellOptions;

  // The shell from --shell or the settings comes first.
  const bool hasConfigured = !m_defaultShellCommand.empty();
  if (hasConfigured) {
    options.insert(options.begin(),
                   {_("Configured shell"), m_defaultShellCommand});
  }

  if (options.empty()) {
    return m_defaultShellCommand;
  }
  if (options.size() == 1) {
    return options[0].command;
  }

  wxArrayString titles;
  int initialSelection = 0;
  for (size_t i = 0; i < options.size(); ++i) {
    titles.Add(options[i].title);
    if (options[i].command == m_config.GetLastShell()) {
      initialSelection = static_cast<int>(i);
    }
  }
  const int choice = wxGetSingleChoiceIndex(_("Available shells"),
                                            _("Choose a shell to launch"),
                                            titles, initialSelection, this);
  if (choice == wxNOT_FOUND) {
    return std::nullopt;
  }
  m_config.SetLastShell(options[static_cast<size_t>(choice)].command);
  PersistSettings();
  return options[static_cast<size_t>(choice)].command;
#else
  return m_defaultShellCommand;
#endif
}

void MyFrame::OnExit(wxCommandEvent &event) {
  wxUnusedVar(event);
  Close(true);
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
      auto clearStatusTextCB = [this, count = size_t{0}]() mutable {
        if (++count >= 3) {
          GetStatusBar()->SetStatusText(wxEmptyString);
          return true;
        }
        return false;
      };
      m_timerCallbacks.push_back(std::move(clearStatusTextCB));
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
    if (TerminalOf(m_notebook->GetPage(i)) == view) {
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
  wxString text = event.GetClickedText();

  while (text.EndsWith(".") || text.EndsWith(":") || text.EndsWith(","))
    text.RemoveLast();

  // Nothing is left of a click on "." or "..", for example.
  if (text.empty())
    return;

  wxString clickedText = text;
  wxString clickedTextLowerCase = text.Lower();

  if (clickedTextLowerCase.StartsWith("http://") ||
      clickedTextLowerCase.StartsWith("https://")) {
    ::wxLaunchDefaultBrowser(clickedText);
  } else if (clickedText.StartsWith("file://")) {
    ::wxLaunchDefaultApplication(clickedText);
  } else {
    // Try the file's path:
    wxTerminalViewCtrl *ctrl =
        dynamic_cast<wxTerminalViewCtrl *>(event.GetEventObject());
    if (ctrl == nullptr)
      return;
    TryOpenPath(clickedText, ctrl->GetCurrentDir());
  }
}

void MyFrame::OnCwdChanged(wxTerminalEvent &event) {
  event.Skip();
  // Tabs in the background must not overwrite the active tab's directory.
  if (event.GetEventObject() == GetActiveTerminalView()) {
    UpdateDirectoryStatus();
  }
}

void MyFrame::UpdateDirectoryStatus() {
  auto *statusBar = GetStatusBar();
  if (!statusBar || statusBar->GetFieldsCount() <= kStatusFieldDirectory) {
    return;
  }
  auto *view = GetActiveTerminalView();
  statusBar->SetStatusText(view ? view->GetCurrentDir() : wxString{},
                           kStatusFieldDirectory);
}

// The terminal view draws the progress bar. The status bar only says which
// event the active terminal received last.
void MyFrame::UpdateProgressStatus() {
  auto *statusBar = GetStatusBar();
  if (!statusBar || statusBar->GetFieldsCount() <= kStatusFieldProgress) {
    return;
  }
  wxString text;
  if (auto *view = GetActiveTerminalView()) {
    const auto progress = view->GetProgressState();
    const auto &status = view->GetProgramStatus();
    if (progress != wxTerminalProgressState::Hidden) {
      static const char *const kNames[] = {"hidden", "normal", "error",
                                           "indeterminate", "warning"};
      text = wxString::Format("OSC 9;4 event: %s %d%%",
                              kNames[static_cast<int>(progress)],
                              view->GetProgressValue());
    } else if (status.state != wxTerminalProgramState::Clear) {
      static const char *const kNames[] = {"idle",    "working", "done",
                                           "blocked", "error",   "clear"};
      text = wxString::Format("OSC 7501 event: %s",
                              kNames[static_cast<int>(status.state)]);
      if (status.progress >= 0) {
        text << wxString::Format(" %d%%", status.progress);
      }
      if (!status.app.empty()) {
        text << " (" << status.app << ")";
      }
    }
  }
  statusBar->SetStatusText(text, kStatusFieldProgress);
}

void MyFrame::OnNotification(wxTerminalEvent &event) {
  wxNotificationMessage notification(event.GetTitle(), event.GetString(), this);
  notification.Show();

  // The same text in the status bar for 3 seconds.
  if (GetStatusBar()) {
    wxString text = event.GetTitle();
    if (!text.empty() && !event.GetString().empty()) {
      text += wxS(": ");
    }
    text += event.GetString();
    GetStatusBar()->SetStatusText(text);
    auto clearStatusTextCB = [this, count = size_t{0}]() mutable {
      if (++count >= 3) {
        GetStatusBar()->SetStatusText(wxEmptyString);
        return true;
      }
      return false;
    };
    m_timerCallbacks.push_back(std::move(clearStatusTextCB));
  }
}

void MyFrame::OnAppSelectionChanged(wxTerminalEvent &event) {
  event.Skip();
  // Tabs in the background must not overwrite the active tab's text, nor the
  // clipboard: a program there could put a command on it without the user
  // seeing it.
  if (event.GetEventObject() != GetActiveTerminalView()) {
    return;
  }
  UpdateAppSelectionStatus();
  // The program copied text with OSC 52: put it on the system clipboard. An
  // empty string means the text was cleared, which leaves the clipboard alone.
  const wxString &text = event.GetString();
  if (!text.empty() && wxTheClipboard->Open()) {
    wxTheClipboard->SetData(new wxTextDataObject(text));
    wxTheClipboard->Flush();
    wxTheClipboard->Close();
  }
}

void MyFrame::UpdateAppSelectionStatus() {
  auto *statusBar = GetStatusBar();
  if (!statusBar || statusBar->GetFieldsCount() <= kStatusFieldAppSelection) {
    return;
  }
  // The field stays empty when there is no selection.
  wxString text;
  if (auto *view = GetActiveTerminalView()) {
    if (auto selection = view->GetAppSelection()) {
      constexpr size_t kMaxLen = 80;
      text = wxString::Format(_("App Selection: %s"),
                              ToStatusBarText(*selection, kMaxLen));
    }
  }
  statusBar->SetStatusText(text, kStatusFieldAppSelection);
}

void MyFrame::OnBell(wxTerminalEvent &event) {
  if (m_bellCallbackInstalled) {
    return;
  }

  auto view = dynamic_cast<wxTerminalViewCtrl *>(event.GetEventObject());
  if (!view || !m_notebook) {
    return;
  }

  int sel = m_notebook->FindPage(view);
  if (sel == wxNOT_FOUND) {
    return;
  }

  wxString page_title = m_notebook->GetPageText(sel);
  m_notebook->SetPageText(sel, page_title + wxT(" \U0001F514 "));
  auto restore_title = [view, page_title, this]() {
    m_bellCallbackInstalled = false;
    int where = m_notebook->FindPage(view);
    if (where == wxNOT_FOUND) {
      return true;
    }
    m_notebook->SetPageText(where, page_title);
    return true;
  };
  m_bellCallbackInstalled = true;
  m_timerCallbacks.push_back(std::move(restore_title));
}

void MyFrame::OnTimer(wxTimerEvent &event) {
  wxUnusedVar(event);
  std::deque<std::function<bool()>> tempQueue;
  while (!m_timerCallbacks.empty()) {
    auto cb = std::move(m_timerCallbacks.front());
    m_timerCallbacks.pop_front();
    if (!cb()) {
      tempQueue.push_back(std::move(cb));
    }
  }
  m_timer.StartOnce(1000);
  m_timerCallbacks.swap(tempQueue);
}

void MyFrame::Terminate() { Close(true); }

void MyFrame::PersistSettings() {
  if (!m_notebook)
    return;
  if (!m_config.GetFont().IsOk()) {
    m_config.SetFont(GetActiveTerminalView()
                         ? GetActiveTerminalView()->GetTheme().font
                         : wxFont{});
  }
  AppPersistence::Save(m_config);
}

void MyFrame::ApplyNativeAppTheme() {
#if wxCHECK_VERSION(3, 3, 0)
  // --appearance=light|dark wins. Otherwise follow the active terminal theme.
  const bool enableDark =
      m_cliForceDark.value_or(IsDark(m_config.GetActiveTheme().bg));
  if (enableDark) {
    wxTheApp->SetAppearance(wxAppBase::Appearance::Dark);
  } else {
    wxTheApp->SetAppearance(wxAppBase::Appearance::Light);
  }
#endif
}

void MyFrame::OnSettings(wxCommandEvent &event) {
  wxUnusedVar(event);
  // Preview the theme in all tabs while the dialog is open.
  SettingsDlg dlg{
      this, m_config,
      [this](const wxTerminalTheme &theme, const wxString &outlineColour) {
        ApplyThemeToAllTabs(theme, outlineColour);
      }};
  const bool accepted = dlg.ShowModal() == wxID_OK;

  // The user already sees the previewed theme, so keep it however the dialog
  // is closed. The other settings are kept only with OK.
  m_config.SetTheme(dlg.GetThemeName(), dlg.GetTheme());
  m_config.SetThemeName(dlg.GetThemeName());
  if (accepted) {
    m_config.SetNewTabTitle(dlg.GetNewTabTitle());
    m_config.SetShowCloseButton(dlg.GetShowCloseButton());
    m_config.SetBlockCursor(dlg.GetBlockCursor());
    m_config.SetTabOutlineColour(dlg.GetTabOutlineColour());
    m_config.SetTabPosition(dlg.GetTabPosition());
    m_config.SetTabStyle(dlg.GetTabStyle());
    m_config.SetFont(dlg.GetFont());
    m_config.SetShellExecutable(dlg.GetShellExecutable());
    m_config.SetShellArgs(dlg.GetShellArgs());
    if (!wxTerminalViewCtrl::IsOpenGLEnabled()) {
      m_config.SetSafeDrawingEnabled(!dlg.GetOptimisedPaint());
    }
    // Used by the tabs that are opened from now on. Open tabs keep their shell.
    m_defaultShellCommand = ResolveDefaultShell();
  }
  ApplySettings();
  PersistSettings();
}

wxString MyFrame::ResolveDefaultShell() const {
  return m_cliShellCommand.empty() ? m_config.GetShellCommand()
                                   : m_cliShellCommand;
}
