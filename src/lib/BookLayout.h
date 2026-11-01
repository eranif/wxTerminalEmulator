#pragma once

#include <vector>
#include <wx/colour.h>
#include <wx/string.h>

/**
 * @brief What a tabbed book (see GlyphBook) remembers about its tabs, apart
 *        from the pages themselves.
 *
 * It is a plain struct, so the application decides how to store it. The tabs
 * are in the order of the tabs of the book. The book does not create pages: to
 * restore a layout, the application adds the pages in the same order, then
 * calls RestoreLayout().
 */
struct BookLayout {
  // A group of tabs (see GlyphBook). The tabs of a group are next to each
  // other.
  struct Group {
    wxString name;
    wxColour colour;
    bool collapsed{false};
  };

  struct Tab {
    // The caption of the tab. On restore, an empty label keeps the caption the
    // page has.
    wxString label;
    // The index of the group of the tab in BookLayout::groups, or -1 for a tab
    // outside any group.
    int group{-1};
    // True for the current tab. At most one tab is selected.
    bool selected{false};
  };

  // The groups. The tabs refer to them by their index.
  std::vector<Group> groups;
  std::vector<Tab> tabs;
};
