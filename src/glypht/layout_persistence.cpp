#include "layout_persistence.h"

#include "app_persistence.h"

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/textfile.h>
#include <wx/tokenzr.h>

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

namespace {
// "1,2,3" for a list of indexes.
wxString JoinInts(const std::vector<int> &values) {
  wxString out;
  for (size_t i = 0; i < values.size(); ++i) {
    out << (i ? "," : "") << values[i];
  }
  return out;
}

std::vector<int> SplitInts(const wxString &value) {
  std::vector<int> out;
  wxStringTokenizer tokens(value, ",");
  while (tokens.HasMoreTokens()) {
    long number = 0;
    if (tokens.GetNextToken().Trim().Trim(false).ToLong(&number)) {
      out.push_back(static_cast<int>(number));
    }
  }
  return out;
}
} // namespace

bool SaveLayoutFile(
    const std::vector<LayoutPersistence::TabInfo> &tabs,
    const std::vector<LayoutPersistence::TabControlInfo> &tabControls,
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
  for (const auto &control : tabControls) {
    file.AddLine("[tabctrl]");
    file.AddLine(wxString::Format("dock=%d,%d,%d,%d,%d,%d",
                                  control.dockDirection, control.dockLayer,
                                  control.dockRow, control.dockPos,
                                  control.dockProportion, control.dockSize));
    file.AddLine("pages=" + JoinInts(control.pages));
    if (!control.pinned.empty()) {
      file.AddLine("pinned=" + JoinInts(control.pinned));
    }
    file.AddLine(wxString::Format("active=%d", control.active));
  }
  for (const auto &tab : tabs) {
    file.AddLine("[tab]");
    file.AddLine("shell=" + Encode(tab.shellCommand));
    file.AddLine("cwd=" + Encode(tab.workingDirectory));
    file.AddLine("label=" + Encode(tab.customLabel));
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
  enum class Section { Header, Tab, TabControl, Ignored };
  Section section = Section::Header;
  LayoutPersistence::TabInfo current;
  LayoutPersistence::TabControlInfo currentControl;
  // End the section that is open: the tab or the tab control is complete.
  auto flush = [&]() {
    if (section == Section::Tab) {
      result.tabs.push_back(current);
      current = LayoutPersistence::TabInfo{};
    } else if (section == Section::TabControl) {
      result.tabControls.push_back(currentControl);
      currentControl = LayoutPersistence::TabControlInfo{};
    }
    section = Section::Header;
  };

  for (size_t i = 0; i < file.GetLineCount(); ++i) {
    wxString s = file.GetLine(i);
    wxString trimmed = s;
    trimmed.Trim(true).Trim(false);
    if (trimmed.empty() || trimmed.StartsWith("#") || trimmed.StartsWith(";")) {
      continue;
    }

    if (trimmed == "[tab]") {
      flush();
      section = Section::Tab;
      continue;
    }
    if (trimmed == "[tabctrl]") {
      flush();
      section = Section::TabControl;
      continue;
    }
    if (trimmed == "[group]" || trimmed == "[layout]") {
      // The tab groups and the tab controls of older versions: not used.
      flush();
      section = Section::Ignored;
      continue;
    }

    if (section == Section::Header) {
      // Header-level keys (before the first section).
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
    if (section == Section::Ignored) {
      continue;
    }
    if (section == Section::TabControl) {
      auto &control = currentControl;
      if (key == "dock") {
        const std::vector<int> v = SplitInts(value);
        if (v.size() == 6) {
          control.dockDirection = v[0];
          control.dockLayer = v[1];
          control.dockRow = v[2];
          control.dockPos = v[3];
          control.dockProportion = v[4];
          control.dockSize = v[5];
        }
      } else if (key == "pages") {
        control.pages = SplitInts(value);
      } else if (key == "pinned") {
        control.pinned = SplitInts(value);
      } else if (key == "active") {
        long active = 0;
        if (value.ToLong(&active)) {
          control.active = static_cast<int>(active);
        }
      }
      continue;
    }
    if (key == "shell") {
      current.shellCommand = value;
    } else if (key == "cwd") {
      current.workingDirectory = value;
    } else if (key == "label") {
      current.customLabel = value;
    } else if (key == "selected") {
      current.selected = value == "1";
    }
  }
  flush();

  if (result.IsEmpty()) {
    return std::nullopt;
  }
  return result;
}
