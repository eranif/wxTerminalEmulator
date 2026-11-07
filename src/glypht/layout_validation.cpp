#include "layout_persistence.h"

#include <algorithm>

bool AreTabControlsValid(
    const std::vector<LayoutPersistence::TabControlInfo> &tabControls,
    size_t pageCount) {
  if (tabControls.empty() || pageCount == 0) {
    return false;
  }
  // The values of the dock fields come from a file that can be edited by hand.
  // A direction outside wxAUI_DOCK_TOP..wxAUI_DOCK_CENTER (1..5), or a negative
  // number, can give a pane that is not shown.
  const auto dockOk = [](const LayoutPersistence::TabControlInfo &c) {
    return c.dockDirection >= 1 && c.dockDirection <= 5 && c.dockLayer >= 0 &&
           c.dockRow >= 0 && c.dockPos >= 0 && c.dockProportion >= 0 &&
           c.dockSize >= 0;
  };
  const auto inRange = [pageCount](int index) {
    return index >= 0 && static_cast<size_t>(index) < pageCount;
  };

  std::vector<bool> used(pageCount, false);
  for (const auto &control : tabControls) {
    if (!dockOk(control)) {
      return false;
    }
    if (control.pages.empty()) {
      // All the pages in their natural order: only for a single tab control.
      if (tabControls.size() != 1) {
        return false;
      }
      std::fill(used.begin(), used.end(), true);
      if (!inRange(control.active)) {
        return false;
      }
      for (const int index : control.pinned) {
        if (!inRange(index)) {
          return false;
        }
      }
      continue;
    }

    bool activeFound = false;
    for (const int index : control.pages) {
      if (!inRange(index) || used[static_cast<size_t>(index)]) {
        return false;
      }
      used[static_cast<size_t>(index)] = true;
      activeFound = activeFound || index == control.active;
    }
    if (!activeFound) {
      return false;
    }
    for (const int index : control.pinned) {
      if (std::find(control.pages.begin(), control.pages.end(), index) ==
          control.pages.end()) {
        return false;
      }
    }
  }
  return std::all_of(used.begin(), used.end(), [](bool v) { return v; });
}
