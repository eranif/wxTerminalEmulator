#include "bundled_fonts.h"

#include "terminal_logger.h"
#include "terminal_theme.h"
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/font.h>
#include <wx/fontenum.h>
#include <wx/log.h>
#include <wx/stdpaths.h>

namespace {
// The font that is the default of the terminal (file name and family name).
#ifdef __WXGTK__
// "Iosevka Term" is not known to Pango after wxFont::AddPrivateFont() on Linux:
// use the regular Iosevka.
const wxString kDefaultFontFile = "Iosevka-Regular.ttf";
const wxString kDefaultFontFace = "Iosevka";
#else
const wxString kDefaultFontFile = "IosevkaTerm-Regular.ttf";
const wxString kDefaultFontFace = "Iosevka Term";
#endif
} // namespace

wxString ShippedFontsDir() {
#ifdef __WXMAC__
  wxFileName dir(wxStandardPaths::Get().GetResourcesDir(), "");
  dir.AppendDir("Fonts");
#else
  wxFileName dir(wxStandardPaths::Get().GetExecutablePath());
  dir.SetFullName("");
  dir.AppendDir("fonts");
#endif
  return dir.GetPath();
}

void LoadBundledFonts() {
#if wxUSE_PRIVATE_FONTS
  const wxString dir = ShippedFontsDir();
  if (!wxDir::Exists(dir)) {
    TLOG_INFO() << "No shipped fonts directory: " << dir << std::endl;
    return;
  }

  wxArrayString files;
  wxDir::GetAllFiles(dir, &files, "*.?tf", wxDIR_FILES);
  for (const wxString &file : files) {
    // wxFont::AddPrivateFont() reports a failure with wxLogError(), which is a
    // message box in a GUI program. The result is logged below.
    bool added = false;
    {
      wxLogNull noLog;
      added = wxFont::AddPrivateFont(file);
    }
    if (!added) {
      TLOG_WARN() << "Could not load the font " << file << std::endl;
      continue;
    }
    TLOG_INFO() << "Loaded the font " << file << std::endl;
    if (wxFileName(file).GetFullName() != kDefaultFontFile) {
      continue;
    }
#if wxUSE_FONTENUM
    if (!wxFontEnumerator::IsValidFacename(kDefaultFontFace)) {
      TLOG_WARN() << "The font face " << kDefaultFontFace
                  << " is not known to the system after loading " << file
                  << std::endl;
      continue;
    }
#endif
    wxTerminalTheme::SetBundledFontFace(kDefaultFontFace);
  }
#endif // wxUSE_PRIVATE_FONTS
}
