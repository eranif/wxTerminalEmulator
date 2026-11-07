// Checks AreTabControlsValid(). Build with -DBUILD_TESTS=ON and run
// layout_persistence_test.
#include "layout_persistence.h"

#include <cstdio>

using Control = LayoutPersistence::TabControlInfo;

static int failures = 0;
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::printf("FAIL line %d: %s\n", __LINE__, #cond);                      \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

static Control Make(std::vector<int> pages, int active,
                    std::vector<int> pinned = {}) {
  Control control;
  control.dockDirection = 5; // wxAUI_DOCK_CENTER, the main tab control.
  control.pages = std::move(pages);
  control.active = active;
  control.pinned = std::move(pinned);
  return control;
}

int main() {
  CHECK(!AreTabControlsValid({}, 3));
  CHECK(!AreTabControlsValid({Make({0}, 0)}, 0));

  // A split: every page in exactly one tab control.
  CHECK(AreTabControlsValid({Make({0, 2}, 2), Make({1}, 1)}, 3));
  CHECK(AreTabControlsValid({Make({2, 0, 1}, 0, {1})}, 3));

  // Natural order.
  CHECK(AreTabControlsValid({Make({}, 1)}, 3));
  CHECK(!AreTabControlsValid({Make({}, 3)}, 3));
  CHECK(!AreTabControlsValid({Make({}, 0), Make({1}, 1)}, 3));

  // Bad indexes.
  CHECK(!AreTabControlsValid({Make({0, 1, 3}, 0)}, 3));
  CHECK(!AreTabControlsValid({Make({0, 1, -1}, 0)}, 3));
  // A page in two tab controls.
  CHECK(!AreTabControlsValid({Make({0, 1}, 0), Make({1, 2}, 2)}, 3));
  // A page in no tab control.
  CHECK(!AreTabControlsValid({Make({0, 1}, 0)}, 3));
  // The active page is not in the tab control.
  CHECK(!AreTabControlsValid({Make({0, 1}, 2), Make({2}, 2)}, 3));
  // A pinned page that is not in the tab control.
  CHECK(!AreTabControlsValid({Make({0, 1}, 0, {2}), Make({2}, 2)}, 3));

  // The dock fields.
  {
    Control bad = Make({0, 1, 2}, 0);
    CHECK(AreTabControlsValid({bad}, 3));
    bad.dockDirection = 0;
    CHECK(!AreTabControlsValid({bad}, 3));
    bad.dockDirection = 6;
    CHECK(!AreTabControlsValid({bad}, 3));
    bad.dockDirection = 1;
    CHECK(AreTabControlsValid({bad}, 3));
    bad.dockSize = -1;
    CHECK(!AreTabControlsValid({bad}, 3));
    bad = Make({0, 1, 2}, 0);
    bad.dockProportion = -5;
    CHECK(!AreTabControlsValid({bad}, 3));
    bad = Make({0, 1, 2}, 0);
    bad.dockLayer = -1;
    CHECK(!AreTabControlsValid({bad}, 3));
    bad = Make({0, 1, 2}, 0);
    bad.dockRow = -1;
    CHECK(!AreTabControlsValid({bad}, 3));
    bad = Make({0, 1, 2}, 0);
    bad.dockPos = -1;
    CHECK(!AreTabControlsValid({bad}, 3));
  }

  std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures != 0;
}
