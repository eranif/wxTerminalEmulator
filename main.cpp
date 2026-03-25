#include "terminal_panel.h"

#include <wx/app.h>
#include <wx/frame.h>

class MyFrame : public wxFrame {
public:
  MyFrame() : wxFrame(nullptr, wxID_ANY, "EmbeddedCmdDemo", wxDefaultPosition, wxSize(900, 600)) {
    auto* panel = new TerminalPanel(this);
    panel->StartProcess(""); // Empty string will use default shell (cmd.exe)
  }
};

class MyApp : public wxApp {
public:
  bool OnInit() override {
    auto* frame = new MyFrame();
    frame->Show();
    return true;
  }
};

wxIMPLEMENT_APP(MyApp);
