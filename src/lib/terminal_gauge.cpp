#include "terminal_gauge.h"

#if USE_OPENGL

#include <algorithm>
#include <chrono>
#include <cmath>
#include <wx/dcclient.h>

namespace {

wxGLAttributes GaugeGLAttributes() {
  wxGLAttributes attr;
  attr.PlatformDefaults().RGBA().DoubleBuffer().EndList();
  return attr;
}

// How long the moving bar takes to run from one end to the other.
constexpr double kPulsePeriodMs = 2000.0;

} // namespace

Gauge::Gauge(wxWindow *parent, wxWindowID id, int range, const wxPoint &pos,
             const wxSize &size, long style, const wxString &name)
    : wxGLCanvas(parent, GaugeGLAttributes(), id, pos, size, style, name),
      m_range{std::max(range, 0)} {
  // The same core profile context as the terminal.
  wxGLContextAttrs ctxAttrs;
  ctxAttrs.PlatformDefaults().CoreProfile().OGLVersion(3, 2).EndList();
  m_glContext = std::make_unique<wxGLContext>(this, nullptr, &ctxAttrs);

  // The canvas draws every pixel itself.
  SetBackgroundStyle(wxBG_STYLE_PAINT);
  Bind(wxEVT_PAINT, &Gauge::OnPaint, this);
  Bind(wxEVT_SIZE, &Gauge::OnSize, this);
  Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent &) {});
}

Gauge::~Gauge() {
  // Release the GL resources while the context is current.
  if (m_glContext && m_glInitialized) {
    m_glContext->SetCurrent(*this);
    m_renderer.Destroy();
  }
}

void Gauge::SetRange(int range) {
  range = std::max(range, 0);
  if (range == m_range && !m_pulsing) {
    return;
  }
  m_range = range;
  m_value = std::min(m_value, m_range);
  m_pulsing = false;
  Refresh(false);
}

void Gauge::SetValue(int value) {
  value = std::clamp(value, 0, m_range);
  if (value == m_value && !m_pulsing) {
    return;
  }
  m_value = value;
  m_pulsing = false;
  Refresh(false);
}

void Gauge::Pulse() {
  // The bar moves by the time since the last Pulse(), so its speed does not
  // depend on how often Pulse() is called.
  const auto now = std::chrono::steady_clock::now();
  if (m_pulsing) {
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(now - m_lastPulse).count();
    m_pulsePhase = std::fmod(m_pulsePhase + elapsedMs / kPulsePeriodMs, 1.0);
  }
  m_lastPulse = now;
  m_pulsing = true;
  Refresh(false);
}

bool Gauge::SetForegroundColour(const wxColour &colour) {
  const bool changed = wxGLCanvas::SetForegroundColour(colour);
  Refresh(false);
  return changed;
}

bool Gauge::SetBackgroundColour(const wxColour &colour) {
  const bool changed = wxGLCanvas::SetBackgroundColour(colour);
  Refresh(false);
  return changed;
}

wxSize Gauge::DoGetBestSize() const {
  return IsVertical() ? wxSize(FromDIP(8), FromDIP(100))
                      : wxSize(FromDIP(100), FromDIP(8));
}

void Gauge::OnSize(wxSizeEvent &event) {
  event.Skip();
  Refresh(false);
}

void Gauge::OnPaint(wxPaintEvent &) {
  // A GL canvas must consume the paint event with a wxPaintDC.
  wxPaintDC paintDc{this};

  const wxSize size = GetClientSize();
  if (!m_glContext || !IsShownOnScreen() || size.x <= 0 || size.y <= 0) {
    return;
  }
  m_glContext->SetCurrent(*this);
  if (!m_glInitialized) {
    // The gauge draws only solid quads: no glyph atlas.
    m_glInitialized = m_renderer.Init(false);
    if (!m_glInitialized) {
      return;
    }
  }

  // The bar runs along the main axis: from the left (horizontal) or from the
  // bottom (vertical).
  const bool vertical = IsVertical();
  const int length = vertical ? size.y : size.x;
  int from = 0;
  int to = 0;
  if (m_pulsing) {
    // A bar that runs in from the start and out of the end, again and again.
    const int segment = std::max(length / 4, FromDIP(24));
    from = static_cast<int>(m_pulsePhase * (length + segment)) - segment;
    to = from + segment;
  } else if (m_range > 0) {
    to = static_cast<int>(static_cast<long long>(length) * m_value / m_range);
  }
  from = std::clamp(from, 0, length);
  to = std::clamp(to, 0, length);

#ifdef __WXMSW__
  const double scale = 1.0;
#else
  const double scale = GetDPIScaleFactor();
#endif
  // The track is the background of the frame.
  m_renderer.BeginFrame(size.x, size.y, scale, GetBackgroundColour());
  if (to > from) {
    if (vertical) {
      m_renderer.AddSolidRect(0, size.y - to, size.x, to - from,
                              GetForegroundColour());
    } else {
      m_renderer.AddSolidRect(from, 0, to - from, size.y,
                              GetForegroundColour());
    }
  }
  m_renderer.EndFrame();
  SwapBuffers();
}

#endif // USE_OPENGL
