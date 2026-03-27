#include "terminal_view.h"

#include "terminal_event.h"
#include "terminal_logger.h"
#include <wx/app.h>
#include <wx/display.h>
#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>

class MyFrame : public wxFrame {
public:
  enum {
    ID_ThemeDark = wxID_HIGHEST + 1,
    ID_ThemeLight,
    ID_CenterLine,
    ID_SetSelection,
    ID_PrintLine,
    ID_SendInput
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
    optionsMenu->AppendSeparator();
    optionsMenu->Append(ID_CenterLine, "Center Line...");
    optionsMenu->Append(ID_SetSelection, "Set Selection...");
    optionsMenu->Append(ID_PrintLine, "Print Line...");
    optionsMenu->Append(ID_SendInput, "Send Input...");
    optionsMenu->Check(ID_ThemeDark, true);
    menuBar->Append(optionsMenu, "Options");
    SetMenuBar(menuBar);

    Bind(wxEVT_MENU, &MyFrame::OnDarkTheme, this, ID_ThemeDark);
    Bind(wxEVT_MENU, &MyFrame::OnLightTheme, this, ID_ThemeLight);
    Bind(wxEVT_MENU, &MyFrame::OnCenterLine, this, ID_CenterLine);
    Bind(wxEVT_MENU, &MyFrame::OnSetSelection, this, ID_SetSelection);
    Bind(wxEVT_MENU, &MyFrame::OnPrintLine, this, ID_PrintLine);
    Bind(wxEVT_MENU, &MyFrame::OnSendInput, this, ID_SendInput);
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

  void OnCenterLine(wxCommandEvent &event) {
    wxUnusedVar(event);
    if (!m_view) {
      return;
    }

    const std::size_t totalLines = m_view->GetLineCount();
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

    m_view->CenterLine(static_cast<std::size_t>(lineNumber - 1));
  }

  void OnSetSelection(wxCommandEvent &event) {
    wxUnusedVar(event);
    if (!m_view) {
      return;
    }

    const std::size_t totalLines = m_view->GetLineCount();
    const std::size_t maxCols = m_view->GetTheme().fg.IsOk() ? 0 : 0;

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

    m_view->SetUserSelection(static_cast<std::size_t>(colStart - 1),
                             static_cast<std::size_t>(lineNumber - 1),
                             static_cast<std::size_t>(count));
  }

  void OnPrintLine(wxCommandEvent &event) {
    wxUnusedVar(event);
    if (!m_view) {
      return;
    }

    const std::size_t totalLines = m_view->GetLineCount();
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
        m_view->GetLine(static_cast<std::size_t>(lineNumber - 1));
    wxMessageBox(content.empty() ? "<empty line>" : content, "Line Content",
                 wxOK | wxICON_INFORMATION, this);
  }

  void OnSendInput(wxCommandEvent &event) {
    wxUnusedVar(event);
    if (!m_view) {
      return;
    }

    wxString input = wxGetTextFromUser(
        "Enter text to send to terminal:", "Send Input", wxEmptyString, this);
    if (input.empty()) {
      return;
    }
    m_view->SendInput(input.ToStdString(wxConvUTF8));
    m_view->SendInput("\r"); // Flush it
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
