#include "layout_persistence.h"

#include "app_persistence.h"

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/textfile.h>

namespace {

// Encode a value so it can live on a single "key=value" line: newlines and
// backslashes are escaped. Tabs and '=' are safe because we split on the first
// '=' only and never store tabs literally.
wxString Encode(const wxString &value) {
  wxString out;
  out.reserve(value.size());
  for (wxUniChar ch : value) {
    if (ch == '\\') {
      out += "\\\\";
    } else if (ch == '\n') {
      out += "\\n";
    } else if (ch == '\r') {
      out += "\\r";
    } else {
      out += ch;
    }
  }
  return out;
}

wxString Decode(const wxString &value) {
  wxString out;
  out.reserve(value.size());
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '\\' && i + 1 < value.size()) {
      const wxUniChar next = value[i + 1];
      if (next == '\\') {
        out += '\\';
        ++i;
        continue;
      }
      if (next == 'n') {
        out += '\n';
        ++i;
        continue;
      }
      if (next == 'r') {
        out += '\r';
        ++i;
        continue;
      }
    }
    out += value[i];
  }
  return out;
}

} // namespace

wxString LayoutPersistence::GetLayoutPath() {
  wxFileName fn(AppPersistence::GetConfigPath());
  fn.SetFullName("layout.conf");
  return fn.GetFullPath();
}

void LayoutPersistence::Clear() {
  const wxString path = GetLayoutPath();
  if (wxFileName::FileExists(path)) {
    wxRemoveFile(path);
  }
}

// ---------------------------------------------------------------------------
// File IO
// ---------------------------------------------------------------------------

bool SaveLayoutFile(const std::vector<LayoutPersistence::TabInfo> &tabs,
                    const wxSize &frameSize) {
  const wxString path = LayoutPersistence::GetLayoutPath();
  wxFileName fn(path);
  if (!fn.DirExists()) {
    wxFileName::Mkdir(fn.GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  }

  wxTextFile file;
  if (wxFileName::FileExists(path)) {
    if (!file.Open(path)) {
      return false;
    }
    file.Clear();
  } else if (!file.Create(path)) {
    return false;
  }

  file.AddLine("# GlyphT window layout - generated file");
  if (frameSize.x > 0 && frameSize.y > 0) {
    file.AddLine(wxString::Format("size=%dx%d", frameSize.x, frameSize.y));
  }
  for (const auto &tab : tabs) {
    file.AddLine("[tab]");
    file.AddLine("shell=" + Encode(tab.shellCommand));
    file.AddLine("cwd=" + Encode(tab.workingDirectory));
    file.AddLine("label=" + Encode(tab.customLabel));
    if (!tab.group.empty()) {
      file.AddLine("group=" + Encode(tab.group));
    }
    if (tab.selected) {
      file.AddLine("selected=1");
    }
  }
  return file.Write();
}

std::optional<LayoutFile> LoadLayoutFile() {
  const wxString path = LayoutPersistence::GetLayoutPath();
  if (!wxFileName::FileExists(path)) {
    return std::nullopt;
  }

  wxTextFile file;
  if (!file.Open(path)) {
    return std::nullopt;
  }

  LayoutFile result;
  bool inTab = false;
  LayoutPersistence::TabInfo current;
  auto flushTab = [&]() {
    if (inTab) {
      result.tabs.push_back(current);
      current = LayoutPersistence::TabInfo{};
      inTab = false;
    }
  };

  for (size_t i = 0; i < file.GetLineCount(); ++i) {
    wxString s = file.GetLine(i);
    wxString trimmed = s;
    trimmed.Trim(true).Trim(false);
    if (trimmed.empty() || trimmed.StartsWith("#") || trimmed.StartsWith(";")) {
      continue;
    }

    if (trimmed == "[tab]") {
      flushTab();
      inTab = true;
      continue;
    }
    if (trimmed == "[layout]") {
      // The tab controls of older versions: not used.
      flushTab();
      continue;
    }

    if (!inTab) {
      // Header-level keys (before the first [tab] section).
      if (trimmed.StartsWith("active=")) {
        result.activeTitle = Decode(trimmed.Mid(7));
      } else if (trimmed.StartsWith("size=")) {
        long w = 0, h = 0;
        const wxString value = trimmed.Mid(5);
        if (value.BeforeFirst('x').ToLong(&w) &&
            value.AfterFirst('x').ToLong(&h) && w > 0 && h > 0) {
          result.frameSize = wxSize(static_cast<int>(w), static_cast<int>(h));
        }
      }
      continue;
    }

    const int eq = trimmed.Find('=');
    if (eq == wxNOT_FOUND) {
      continue;
    }
    const wxString key = trimmed.Left(eq).Lower();
    const wxString value = Decode(trimmed.Mid(eq + 1));
    if (key == "shell") {
      current.shellCommand = value;
    } else if (key == "cwd") {
      current.workingDirectory = value;
    } else if (key == "label") {
      current.customLabel = value;
    } else if (key == "group") {
      current.group = value;
    } else if (key == "selected") {
      current.selected = value == "1";
    }
  }
  flushTab();

  if (result.IsEmpty()) {
    return std::nullopt;
  }
  return result;
}
