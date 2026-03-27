#include "terminal_view.h"

#include "terminal_event.h"
#include "terminal_logger.h"
#include <wx/app.h>
#include <wx/display.h>
#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>

class MyFrame : public wxFrame {
public:
  enum {
    ID_ThemeDark = wxID_HIGHEST + 1,
    ID_ThemeLight
  };

  MyFrame() : wxFrame(nullptr, wxID_ANY, "wxTerminalEmulator") {
    // Get the primary display size
    wxDisplay display(wxDisplay::GetFromWindow(this));
    wxRect screen = display.GetClientArea();

    // Set initial size to 1/3 of screen dimensions
    int width = screen.width / 2;
    int height = screen.height / 2;
    SetSize(width, height);
    CentreOnScreen(); // Center the window on screen

    BuildMenuBar();

    TerminalLogger::Get().SetLevel(TerminalLogLevel::kDebug);
    m_view = new TerminalView(this);
    m_view->SetTheme(wxTerminalTheme::MakeDarkTheme());
    m_themeIsDark = true;
    m_view->StartProcess(""); // Empty string will use default shell

    m_view->Bind(wxEVT_TERMINAL_TITLE_CHANGED, &MyFrame::OnTitleChanged, this);
    m_view->Bind(wxEVT_TERMINAL_TERMINATED, &MyFrame::OnTerminated, this);
  }

  void BuildMenuBar() {
    auto *menuBar = new wxMenuBar();
    auto *optionsMenu = new wxMenu();
    optionsMenu->AppendRadioItem(ID_ThemeDark, "Dark theme");
    optionsMenu->AppendRadioItem(ID_ThemeLight, "Light theme");
    optionsMenu->Check(ID_ThemeDark, true);
    menuBar->Append(optionsMenu, "Options");
    SetMenuBar(menuBar);

    Bind(wxEVT_MENU, &MyFrame::OnDarkTheme, this, ID_ThemeDark);
    Bind(wxEVT_MENU, &MyFrame::OnLightTheme, this, ID_ThemeLight);
  }

  void OnDarkTheme(wxCommandEvent &event) {
    wxUnusedVar(event);
    if (m_view) {
      m_view->SetTheme(wxTerminalTheme::MakeDarkTheme());
      m_themeIsDark = true;
    }
  }

  void OnLightTheme(wxCommandEvent &event) {
    wxUnusedVar(event);
    if (m_view) {
      m_view->SetTheme(wxTerminalTheme::MakeLightTheme());
      m_themeIsDark = false;
    }
  }

  void OnTerminated(wxTerminalEvent &event) {
    wxUnusedVar(event);
    ::wxMessageBox("Terminal terminated!", "wxTerminalEmulator",
                   wxICON_WARNING | wxOK | wxCENTER);
    CallAfter(&MyFrame::Terminate);
  }
  void OnTitleChanged(wxTerminalEvent &event) { SetTitle(event.GetTitle()); }
  void Terminate() { Close(true); }

private:
  TerminalView *m_view{nullptr};
  bool m_themeIsDark{true};
};

class MyApp : public wxApp {
public:
  bool OnInit() override {
    auto *frame = new MyFrame();
    frame->Show();
    return true;
  }
};

wxIMPLEMENT_APP(MyApp);
