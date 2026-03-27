#include "terminal_view.h"

#include "terminal_event.h"
#include "terminal_logger.h"
#include <wx/app.h>
#include <wx/display.h>
#include <wx/frame.h>
#include <wx/msgdlg.h>

class MyFrame : public wxFrame {
public:
  MyFrame() : wxFrame(nullptr, wxID_ANY, "wxTerminalEmulator") {
    // Get the primary display size
    wxDisplay display(wxDisplay::GetFromWindow(this));
    wxRect screen = display.GetClientArea();

    // Set initial size to 1/3 of screen dimensions
    int width = screen.width / 2;
    int height = screen.height / 2;
    SetSize(width, height);
    CentreOnScreen(); // Center the window on screen

    TerminalLogger::Get().SetLevel(TerminalLogLevel::kError);
    auto *view = new TerminalView(this);
    view->SetTheme(wxTerminalTheme::MakeDarkTheme());
    view->StartProcess(""); // Empty string will use default shell

    view->Bind(wxEVT_TERMINAL_TITLE_CHANGED, &MyFrame::OnTitleChanged, this);
    view->Bind(wxEVT_TERMINAL_TERMINATED, &MyFrame::OnTerminated, this);
  }

  void OnTerminated(wxTerminalEvent &event) {
    wxUnusedVar(event);
    ::wxMessageBox("Terminal terminated!", "wxTerminalEmulator",
                   wxICON_WARNING | wxOK | wxCENTER);
    CallAfter(&MyFrame::Terminate);
  }
  void OnTitleChanged(wxTerminalEvent &event) { SetTitle(event.GetTitle()); }
  void Terminate() { Close(true); }
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
