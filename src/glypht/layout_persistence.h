#pragma once

#include <optional>
#include <vector>
#include <wx/colour.h>
#include <wx/gdicmn.h>
#include <wx/string.h>

// Persists the terminal tabs across application runs.
//
// For each tab it stores what is needed to recreate the terminal (shell,
// working directory, custom label) and what the book remembers about the tab
// (whether it is the current one, and its group, see BookLayout). The tabs are
// in the order of the book.
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
    // The index of the group of the tab in the list of groups of the file, or
    // -1 for none.
    int group{-1};
    // True for the current tab.
    bool selected{false};
  };

  // A group of tabs (see BookLayout).
  struct GroupInfo {
    wxString name;
    wxColour colour;
    bool collapsed{false};
  };

  static wxString GetLayoutPath();

  // Remove a previously saved layout, if any.
  static void Clear();
};

// Parsed contents of a layout file.
struct LayoutFile {
  std::vector<LayoutPersistence::TabInfo> tabs;
  std::vector<LayoutPersistence::GroupInfo> groups;
  // Title of the current tab, from a file of an older version that did not mark
  // the tab. When several tabs share this title the first one is selected on
  // restore. Empty if unknown.
  wxString activeTitle;
  // Size of the main frame on save, if it was stored.
  std::optional<wxSize> frameSize;

  bool IsEmpty() const { return tabs.empty(); }
};

// Free functions doing the actual file IO. Kept separate from the frame so the
// serialization logic can be unit tested in isolation.
bool SaveLayoutFile(const std::vector<LayoutPersistence::TabInfo> &tabs,
                    const std::vector<LayoutPersistence::GroupInfo> &groups,
                    const wxSize &frameSize);
std::optional<LayoutFile> LoadLayoutFile();
