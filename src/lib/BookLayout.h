#pragma once

#include <vector>
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
  struct Tab {
    // The caption of the tab. On restore, an empty label keeps the caption the
    // page has.
    wxString label;
    // The name of the group of the tab, empty for a tab outside any group.
    // Reserved for tab groups, which are not implemented yet: GlyphBook does
    // not set it and ignores it on restore.
    wxString group;
    // True for the current tab. At most one tab is selected.
    bool selected{false};
  };
  std::vector<Tab> tabs;
};
