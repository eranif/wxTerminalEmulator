#include "app_persistence.h"

#include "terminal_logger.h"
#include <wx/filename.h>
#include <wx/textfile.h>
#include <wx/utils.h>

namespace {
wxString ThemeToString(const wxString &themeName) { return themeName.Lower(); }
} // namespace

wxString AppPersistence::GetConfigPath() {
  wxFileName logFile(TerminalLogger::Get().GetLevel() <=
                             TerminalLogLevel::kTrace
                         ? wxString{}
                         : wxString{});
  wxUnusedVar(logFile);

  wxString path;
  {
    TerminalLogger::Get().SetLevel(TerminalLogger::Get().GetLevel());
  }

  // Derive from the logger's default trace.log location.
#ifdef __WXMSW__
  wxString dir;
  dir << R"(C:\Users\)" << ::wxGetUserId() << wxFileName::GetPathSeparator()
      << ".wxterminal";
#else
  wxString dir =
      wxFileName::GetHomeDir() + wxFileName::GetPathSeparator() + ".wxterminal";
#endif
  return dir + wxFileName::GetPathSeparator() + "config.ini";
}

bool AppPersistence::Load(wxString &themeName, wxFont &font) {
  const wxString path = GetConfigPath();
  if (!wxFileName::FileExists(path))
    return false;

  wxTextFile file;
  if (!file.Open(path))
    return false;

  for (size_t i = 0; i < file.GetLineCount(); ++i) {
    wxString s = file.GetLine(i);
    s = s.Strip(wxString::both);
    if (s.empty() || s.StartsWith("#") || s.StartsWith(";"))
      continue;

    const int eq = s.Find('=');
    if (eq == wxNOT_FOUND)
      continue;

    const wxString key = s.Left(eq).Strip(wxString::both).Lower();
    const wxString value = s.Mid(eq + 1).Strip(wxString::both);
    if (key == "theme") {
      themeName = value;
    } else if (key == "font") {
      font.SetNativeFontInfo(value);
    }
  }
  return true;
}

bool AppPersistence::Load(bool &safeDrawingEnabled) {
  const wxString path = GetConfigPath();
  if (!wxFileName::FileExists(path))
    return false;

  wxTextFile file;
  if (!file.Open(path))
    return false;

  for (size_t i = 0; i < file.GetLineCount(); ++i) {
    wxString s = file.GetLine(i);
    s = s.Strip(wxString::both);
    if (s.empty() || s.StartsWith("#") || s.StartsWith(";"))
      continue;

    const int eq = s.Find('=');
    if (eq == wxNOT_FOUND)
      continue;

    const wxString key = s.Left(eq).Strip(wxString::both).Lower();
    const wxString value = s.Mid(eq + 1).Strip(wxString::both).Lower();
    if (key == "safedrawing") {
      safeDrawingEnabled = (value == "1" || value == "true" || value == "yes" || value == "on");
    }
  }
  return true;
}

bool AppPersistence::Save(const wxString &themeName, const wxFont &font,
                          bool safeDrawingEnabled) {
  const wxString path = GetConfigPath();
  wxFileName fn(path);
  if (!fn.DirExists())
    wxFileName::Mkdir(fn.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

  wxTextFile file;
  if (wxFileName::FileExists(path)) {
    if (!file.Open(path))
      return false;
    file.Clear();
  } else {
    if (!file.Create(path))
      return false;
  }

  file.AddLine("theme=" + ThemeToString(themeName));
  file.AddLine("font=" + font.GetNativeFontInfoDesc());
  file.AddLine(wxString::Format("safedrawing=%s",
                                safeDrawingEnabled ? "true" : "false"));
  return file.Write();
}
