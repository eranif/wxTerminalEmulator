#pragma once

#include <wx/font.h>
#include <wx/string.h>

class AppPersistence {
public:
  static wxString GetConfigPath();
  static bool Load(wxString &themeName, wxFont &font);
  static bool Load(bool &safeDrawingEnabled);
  static bool Save(const wxString &themeName, const wxFont &font,
                   bool safeDrawingEnabled);
};
