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

#if WXTERMINAL_HAS_AUI_SERIALIZATION
// Render an int vector as a comma separated list ("" for an empty vector).
wxString JoinInts(const std::vector<int> &values) {
  wxString out;
  for (size_t i = 0; i < values.size(); ++i) {
    if (i) {
      out += ',';
    }
    out << values[i];
  }
  return out;
}

std::vector<int> SplitInts(const wxString &s) {
  std::vector<int> result;
  wxStringTokenizer tok(s, ",");
  while (tok.HasMoreTokens()) {
    long v = 0;
    if (tok.GetNextToken().ToLong(&v)) {
      result.push_back(static_cast<int>(v));
    }
  }
  return result;
}
#endif // WXTERMINAL_HAS_AUI_SERIALIZATION

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

#if WXTERMINAL_HAS_AUI_SERIALIZATION
// ---------------------------------------------------------------------------
// LayoutSerializer
// ---------------------------------------------------------------------------

void LayoutSerializer::BeforeSaveNotebook(const wxString &name) {
  wxUnusedVar(name);
}

void LayoutSerializer::SaveNotebookTabControl(const wxAuiTabLayoutInfo &tab) {
  // Compact, single-line encoding of one tab control. Fields are separated by
  // ';' and the page/pinned lists use ',' internally.
  wxString line;
  line << "tabcontrol=" << tab.dock_direction << ';' << tab.dock_layer << ';'
       << tab.dock_row << ';' << tab.dock_pos << ';' << tab.dock_proportion
       << ';' << tab.dock_size << ';' << tab.active << ';'
       << "pages:" << JoinInts(tab.pages) << ';'
       << "pinned:" << JoinInts(tab.pinned);
  m_lines.push_back(line);
}

// ---------------------------------------------------------------------------
// LayoutDeserializer
// ---------------------------------------------------------------------------

std::vector<wxAuiTabLayoutInfo>
LayoutDeserializer::LoadNotebookTabs(const wxString &name) {
  wxUnusedVar(name);
  std::vector<wxAuiTabLayoutInfo> tabs;
  for (const wxString &raw : m_lines) {
    wxString value = raw.AfterFirst('=');
    wxArrayString fields = wxStringTokenize(value, ";", wxTOKEN_RET_EMPTY_ALL);
    if (fields.size() < 7) {
      continue;
    }
    wxAuiTabLayoutInfo tab;
    long v = 0;
    if (fields[0].ToLong(&v))
      tab.dock_direction = static_cast<int>(v);
    if (fields[1].ToLong(&v))
      tab.dock_layer = static_cast<int>(v);
    if (fields[2].ToLong(&v))
      tab.dock_row = static_cast<int>(v);
    if (fields[3].ToLong(&v))
      tab.dock_pos = static_cast<int>(v);
    if (fields[4].ToLong(&v))
      tab.dock_proportion = static_cast<int>(v);
    if (fields[5].ToLong(&v))
      tab.dock_size = static_cast<int>(v);
    if (fields[6].ToLong(&v))
      tab.active = static_cast<int>(v);
    for (size_t i = 7; i < fields.size(); ++i) {
      if (fields[i].StartsWith("pages:")) {
        tab.pages = SplitInts(fields[i].Mid(6));
      } else if (fields[i].StartsWith("pinned:")) {
        tab.pinned = SplitInts(fields[i].Mid(7));
      }
    }
    tabs.push_back(std::move(tab));
  }
  return tabs;
}
#endif // WXTERMINAL_HAS_AUI_SERIALIZATION

// ---------------------------------------------------------------------------
// File IO
// ---------------------------------------------------------------------------

bool SaveLayoutFile(const std::vector<LayoutPersistence::TabInfo> &tabs,
                    const std::vector<wxString> &tabControlLines,
                    const wxString &activeTitle) {
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

  file.AddLine("# wxTerminalEmulator window layout - generated file");
  if (!activeTitle.empty()) {
    file.AddLine("active=" + Encode(activeTitle));
  }
  for (const auto &tab : tabs) {
    file.AddLine("[tab]");
    file.AddLine("shell=" + Encode(tab.shellCommand));
    file.AddLine("cwd=" + Encode(tab.workingDirectory));
    file.AddLine("label=" + Encode(tab.customLabel));
  }
  if (!tabControlLines.empty()) {
    file.AddLine("[layout]");
    for (const wxString &line : tabControlLines) {
      file.AddLine(line);
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
      flushTab();
      continue;
    }

    if (trimmed.StartsWith("tabcontrol=")) {
      result.tabControlLines.push_back(trimmed);
      continue;
    }

    if (!inTab) {
      // Header-level keys (before the first [tab] section).
      if (trimmed.StartsWith("active=")) {
        result.activeTitle = Decode(trimmed.Mid(7));
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
    }
  }
  flushTab();

  if (result.IsEmpty()) {
    return std::nullopt;
  }
  return result;
}
