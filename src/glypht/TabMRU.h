#pragma once

#include <algorithm>
#include <vector>

class wxWindow;

// Keeps the tab pages in most-recently-used order. The first page is the most
// recently used one. Pages are only compared by pointer, never dereferenced.
class TabMRU {
public:
  using Pages = std::vector<wxWindow *>;

  // Add a new page. An active page becomes the most recent one. A background
  // page was never used, so it goes to the end. Also drops a stale entry if
  // the address of a closed page was reused.
  void AddPage(wxWindow *page, bool active) {
    RemovePage(page);
    if (active) {
      m_pages.insert(m_pages.begin(), page);
    } else {
      m_pages.push_back(page);
    }
  }

  // Make `page` the most recently used page.
  void Touch(wxWindow *page) { AddPage(page, true); }

  void RemovePage(wxWindow *page) { std::erase(m_pages, page); }

  // Make the list match the pages that exist: drop closed pages, put `current`
  // first and append pages that are not known yet.
  void Sync(const Pages &existing, wxWindow *current) {
    std::erase_if(m_pages, [&existing](wxWindow *page) {
      return std::find(existing.begin(), existing.end(), page) ==
             existing.end();
    });
    if (current) {
      Touch(current);
    }
    for (wxWindow *page : existing) {
      if (std::find(m_pages.begin(), m_pages.end(), page) == m_pages.end()) {
        m_pages.push_back(page);
      }
    }
  }

  const Pages &GetPages() const { return m_pages; }

private:
  Pages m_pages;
};
