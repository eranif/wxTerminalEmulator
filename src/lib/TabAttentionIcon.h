#pragma once

#include <cstddef>
#include <functional>
#include <vector>
#include <wx/bmpbndl.h>
#include <wx/event.h>
#include <wx/timer.h>
#include <wx/toplevel.h>
#include <wx/window.h>

/// How TabAttentionIcon (and TabStatusIcon) reach the tabs of a book. Books do
/// not share the same API for the tab icons, so this describes what is needed.
/// Use From() to get one for a book.
struct TabIconBook {
  /// The index of the page, or wxNOT_FOUND.
  std::function<int(const wxWindow *)> findPage;
  /// The icon of the tab.
  std::function<wxBitmapBundle(int)> getBitmap;
  /// Set the icon of the tab.
  std::function<void(int, const wxBitmapBundle &)> setBitmap;
  /// Is the user looking at this page (selected, and the window is active)?
  std::function<bool(const wxWindow *)> isCurrentPage;

  bool IsOk() const {
    return findPage && getBitmap && setBitmap && isCurrentPage;
  }

  /// Get the description of @p book (a wxAuiNotebook, ...). The book must
  /// outlive the result. A book without SetPageBitmap() gives an object that is
  /// not ok: no icon is shown on its tabs.
  template <typename Book> static TabIconBook From(Book *book) {
    TabIconBook result;
    if constexpr (requires(Book &b, const wxBitmapBundle &bmp) {
                    b.SetPageBitmap(size_t{0}, bmp);
                  }) {
      result.findPage = [book](const wxWindow *page) {
        return book->FindPage(page);
      };
      result.getBitmap = [book](int index) {
        return wxBitmapBundle(book->GetPageBitmap(static_cast<size_t>(index)));
      };
      result.setBitmap = [book](int index, const wxBitmapBundle &bmp) {
        book->SetPageBitmap(static_cast<size_t>(index), bmp);
      };
      // The user looks at the page only if its tab is selected and the window
      // is the active one.
      result.isCurrentPage = [book](const wxWindow *page) {
        auto *top =
            wxDynamicCast(wxGetTopLevelParent(book), wxTopLevelWindow);
        return book->GetCurrentPage() == page && (!top || top->IsActive());
      };
    }
    return result;
  }
};

/// Puts an icon on the tab of @p page to tell the user that the page needs
/// them, or to show a state (a spinner). The icon can be animated: it is made
/// of frames that are shown one after the other.
///
/// The icon is removed, and the icon that was on the tab before is restored,
/// when a window inside the page gets the keyboard focus (unless
/// @p dismissOnFocus is false), or when this object is destroyed. If the page
/// is destroyed first, this object does nothing any more.
class TabAttentionIcon : public wxEvtHandler {
public:
  /// How long each frame is shown, unless the caller asks for another time.
  static constexpr int kDefaultFrameIntervalMs = 100;

  /// @param frames The bitmaps to show, in order. They are shown in a loop.
  ///               With one bitmap there is no animation. Nothing is shown if
  ///               the list has no valid bitmap.
  /// @param dismissOnFocus Remove the icon when the page gets the focus. Use
  ///                       false for an icon that shows a state (a spinner) and
  ///                       not a request to the user.
  /// @param frameIntervalMs How long each frame is shown.
  TabAttentionIcon(TabIconBook book, wxWindow *page,
                   const std::vector<wxBitmapBundle> &frames,
                   bool dismissOnFocus = true,
                   int frameIntervalMs = kDefaultFrameIntervalMs);
  ~TabAttentionIcon() override;

  TabAttentionIcon(const TabAttentionIcon &) = delete;
  TabAttentionIcon &operator=(const TabAttentionIcon &) = delete;

  /// Is the icon on the tab?
  bool IsShown() const { return m_shown; }

  /// Remove the icon and restore the previous one. Does nothing if the icon is
  /// not shown.
  void Dismiss();

private:
  void OnChildFocus(wxChildFocusEvent &event);
  void OnTimer(wxTimerEvent &event);
  void OnPageDestroyed(wxWindowDestroyEvent &event);
  /// Show the frame number @p index on the tab.
  void ShowFrame(size_t index);

  TabIconBook m_book;
  wxWindow *m_page{nullptr}; // nullptr after the page was destroyed
  std::vector<wxBitmapBundle> m_frames;
  size_t m_currentFrame{0};
  wxTimer m_timer;
  wxBitmapBundle m_saved;
  bool m_shown{false};
  bool m_dismissOnFocus{true};
  int m_frameIntervalMs{kDefaultFrameIntervalMs};
};
