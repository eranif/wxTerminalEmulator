#include "TabStatusIcon.h"

#include "spinner_svg.h"
#include "terminal_theme.h"

#include <array>
#include <string_view>
#include <utility>
#include <wx/settings.h>
#include <wx/window.h>

namespace {
constexpr int kIconSize = 16;

// The size of FromSVG() is in DIPs: the bundle scales it to the window itself,
// so it must not be converted with FromDIP() (that would scale it twice).
wxBitmapBundle MakeIcon(std::string_view svg) {
  return wxBitmapBundle::FromSVG(svg.data(), wxSize(kIconSize, kIconSize));
}

bool IsDarkBar() {
  return wxTerminalTheme::IsDark(
      wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));
}

// The frames of an animation, for the dark or the light appearance.
std::vector<wxBitmapBundle>
Frames(const std::array<std::string_view, spinner_svg::kFrameCount> &dark,
       const std::array<std::string_view, spinner_svg::kFrameCount> &light) {
  const auto &svgs = IsDarkBar() ? dark : light;
  std::vector<wxBitmapBundle> frames;
  frames.reserve(svgs.size());
  for (const std::string_view svg : svgs) {
    frames.push_back(MakeIcon(svg));
  }
  return frames;
}
} // namespace

TabStatusIcon::TabStatusIcon(TabIconBook book, wxWindow *page)
    : m_book(std::move(book)), m_page(page) {
  if (m_page != nullptr) {
    m_page->Bind(wxEVT_DESTROY, &TabStatusIcon::OnPageDestroyed, this);
  }
}

TabStatusIcon::~TabStatusIcon() {
  // Restore the icon of the tab, while the page is still there (at most one of
  // the icons is shown).
  m_attention.reset();
  m_spinner.reset();
  if (m_page != nullptr) {
    m_page->Unbind(wxEVT_DESTROY, &TabStatusIcon::OnPageDestroyed, this);
  }
}

void TabStatusIcon::SetState(wxTerminalProgramState state) {
  if (m_page == nullptr) {
    return;
  }

  // Only a change is handled (a working program reports its progress again and
  // again).
  if (std::exchange(m_state, state) == state) {
    return;
  }

  switch (state) {
  case wxTerminalProgramState::Working:
    // An icon that asks for the user is out of date now. Remove it first, so
    // the spinner finds the original icon.
    m_attention.reset();
    ShowSpinner();
    break;

  case wxTerminalProgramState::Blocked:
  case wxTerminalProgramState::Done:
    // Remove the spinner first, so the new icon finds the original icon.
    m_spinner.reset();
    ShowAttention(state);
    break;

  case wxTerminalProgramState::Idle:
  case wxTerminalProgramState::Error:
  case wxTerminalProgramState::Clear:
    // Nothing is going on. The pulse (the program waited for the user) is out
    // of date. A check mark stays until the user comes back to the page.
    m_spinner.reset();
    if (m_attentionState == wxTerminalProgramState::Blocked) {
      m_attention.reset();
    }
    break;
  }
}

void TabStatusIcon::ShowSpinner() {
  if (m_spinner) {
    return; // already spinning
  }
  m_spinner = std::make_unique<TabAttentionIcon>(
      m_book, m_page,
      Frames(spinner_svg::kSpinnerDark, spinner_svg::kSpinnerLight), false,
      kFrameIntervalMs);
}

void TabStatusIcon::ShowAttention(wxTerminalProgramState state) {
  // Replacing the icon first restores the original tab icon.
  m_attention.reset();
  m_attentionState = state;

  const bool blocked = state == wxTerminalProgramState::Blocked;
  // The pulse is always shown, like the spinner. The check mark is not shown to
  // a user who looks at the page (it is selected and the window is active).
  if (!m_book.IsOk() || (!blocked && m_book.isCurrentPage(m_page))) {
    return;
  }

  // A pulse: the program waits for the user. A check mark: the program is
  // done.
  std::vector<wxBitmapBundle> frames;
  if (blocked) {
    frames = Frames(spinner_svg::kBlockedDark, spinner_svg::kBlockedLight);
  } else {
    frames.push_back(MakeIcon(IsDarkBar() ? spinner_svg::kDoneDark
                                          : spinner_svg::kDoneLight));
  }
  m_attention = std::make_unique<TabAttentionIcon>(m_book, m_page, frames,
                                                   /*dismissOnFocus=*/!blocked,
                                                   kFrameIntervalMs);
}

void TabStatusIcon::OnPageDestroyed(wxWindowDestroyEvent &event) {
  event.Skip();
  // wxEVT_DESTROY is a command event: it is sent to the parents of a destroyed
  // window as well. Ignore the events of the children of the page.
  if (event.GetEventObject() != m_page) {
    return;
  }
  // The page (and its tab) is being destroyed: do not touch it, or the book,
  // any more.
  m_spinner.reset();
  m_attention.reset();
  m_page = nullptr;
}
