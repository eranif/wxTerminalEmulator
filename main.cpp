#include "terminal_panel.h"

#include "terminal_logger.h"
#include <wx/app.h>
#include <wx/display.h>
#include <wx/frame.h>

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
    Centre(); // Center the window on screen

    TerminalLogger::Get().SetLevel(TerminalLogLevel::WARN);
    auto *panel = new TerminalPanel(this);
    panel->StartProcess(""); // Empty string will use default shell (cmd.exe)
  }
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
