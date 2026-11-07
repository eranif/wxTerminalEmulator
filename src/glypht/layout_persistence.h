#pragma once

#include <optional>
#include <vector>
#include <wx/gdicmn.h>
#include <wx/string.h>

// Persists the terminal tabs across application runs.
//
// For each tab it stores what is needed to recreate the terminal (shell,
// working directory, custom label) and whether it is the current one. The tabs
// are in the order of the pages of the notebook. It also stores how the tabs
// are arranged in the notebook: the tab controls (the tabs of a notebook can be
// split and docked on any side of it) and the pages of each of them.
//
// The layout is written to ~/.glypht/layout.conf as a small line-based
// text file so it stays human-readable and easy to evolve.
class LayoutPersistence {
public:
  // Description of a single terminal tab needed to recreate it on startup.
  struct TabInfo {
    wxString shellCommand;
    wxString workingDirectory;
    wxString customLabel;
    // True for the current tab.
    bool selected{false};
  };

  // One tab control of the notebook (the same as wxAuiTabLayoutInfo). The
  // first one is the main one.
  struct TabControlInfo {
    // Where the tab control is docked in the notebook.
    int dockDirection{0};
    int dockLayer{0};
    int dockRow{0};
    int dockPos{0};
    int dockProportion{0};
    int dockSize{0};
    // The pages of the tab control, as indexes of the tabs, in display order.
    // Empty: all the pages in their natural order.
    std::vector<int> pages;
    // The pages that are pinned.
    std::vector<int> pinned;
    // The active page of the tab control, as an index of the tab.
    int active{0};
  };

  static wxString GetLayoutPath();

  // Remove a previously saved layout, if any.
  static void Clear();
};

// Parsed contents of a layout file.
struct LayoutFile {
  std::vector<LayoutPersistence::TabInfo> tabs;
  // How the tabs are arranged. Empty for a file without it: the tabs are then
  // in one tab control.
  std::vector<LayoutPersistence::TabControlInfo> tabControls;
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
bool SaveLayoutFile(
    const std::vector<LayoutPersistence::TabInfo> &tabs,
    const std::vector<LayoutPersistence::TabControlInfo> &tabControls,
    const wxSize &frameSize);
std::optional<LayoutFile> LoadLayoutFile();
