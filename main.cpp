#include "app_persistence.h"
#include "terminal_event.h"
#include "terminal_logger.h"
#include "terminal_view.h"
#include "MainFrame.h"

#include <deque>
#include <optional>
#include <stdexcept>
#include <wx/app.h>
#include <wx/aui/auibook.h>
#include <wx/bmpbndl.h>
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
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/stdpaths.h>
#include <wx/string.h>
#include <wx/sysopt.h>
#include <wx/textdlg.h>
#include <wx/tokenzr.h>
#include <wx/xrc/xmlres.h>

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
    std::optional<EnvironmentList> environment{std::nullopt};
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
