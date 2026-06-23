#pragma once

#include <optional>
#include <vector>
#include <wx/aui/serializer.h>
#include <wx/string.h>

// Persists the terminal notebook layout across application runs.
//
// Two kinds of state are stored:
//   1. Per-tab metadata (shell command, working directory, custom label) which
//      is required to recreate the terminals themselves. The wxAUI serializer
//      does not capture this - it only knows about page indices.
//   2. The wxAuiNotebook tab-control arrangement (split tab controls, page
//      order within each control and the active page), captured through the
//      standard wxAuiBookSerializer / wxAuiBookDeserializer interfaces.
//
// The layout is written to ~/.wxterminal/layout.conf as a small line-based
// text file so it stays human-readable and easy to evolve.
class LayoutPersistence {
public:
  // Description of a single terminal tab needed to recreate it on startup.
  struct TabInfo {
    wxString shellCommand;
    wxString workingDirectory;
    wxString customLabel;
  };

  static wxString GetLayoutPath();

  // Remove a previously saved layout, if any.
  static void Clear();
};

// Serializer that captures the wxAuiNotebook tab-control layout into the
// line-based format understood by LayoutDeserializer.
//
// Only the notebook (book) portion of the wxAuiSerializer interface is used;
// the application does not employ floating/docked panes.
class LayoutSerializer : public wxAuiBookSerializer {
public:
  void BeforeSaveNotebook(const wxString &name) override;
  void SaveNotebookTabControl(const wxAuiTabLayoutInfo &tab) override;

  // The accumulated, ready-to-write layout lines.
  const std::vector<wxString> &GetLines() const { return m_lines; }

private:
  std::vector<wxString> m_lines;
};

// Deserializer that reads the tab-control layout produced by LayoutSerializer
// and feeds it back to wxAuiNotebook::LoadLayout().
class LayoutDeserializer : public wxAuiBookDeserializer {
public:
  explicit LayoutDeserializer(std::vector<wxString> tabControlLines)
      : m_lines(std::move(tabControlLines)) {}

  std::vector<wxAuiTabLayoutInfo>
  LoadNotebookTabs(const wxString &name) override;

private:
  std::vector<wxString> m_lines;
};

// Parsed contents of a layout file.
struct LayoutFile {
  std::vector<LayoutPersistence::TabInfo> tabs;
  // Raw "tabcontrol=" lines, passed through to LayoutDeserializer.
  std::vector<wxString> tabControlLines;
  // Title of the tab that was active on save. When several tabs share this
  // title the first one is selected on restore. Empty if unknown.
  wxString activeTitle;

  bool IsEmpty() const { return tabs.empty(); }
};

// Free functions doing the actual file IO. Kept separate from the frame so the
// serialization logic can be unit tested in isolation.
bool SaveLayoutFile(const std::vector<LayoutPersistence::TabInfo> &tabs,
                    const std::vector<wxString> &tabControlLines,
                    const wxString &activeTitle);
std::optional<LayoutFile> LoadLayoutFile();
