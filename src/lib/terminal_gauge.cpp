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

constexpr double kPi = 3.14159265358979323846;

// How long the comet takes to run from one end to the other. It then runs back
// in the same time.
constexpr double kPulseSweepMs = 1400.0;

// The normal mode: how fast the bar follows a new value (the time constant of
// the smoothing), how long a full bar stays, and how long it takes to fade out.
constexpr double kFollowMs = 60.0;
constexpr double kHoldMs = 300.0;
constexpr double kFadeMs = 350.0;
constexpr int kAnimIntervalMs = 8;

double SmoothStep(double edge0, double edge1, double x) {
  const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

// Slow near the ends, fast in the middle.
double EaseInOut(double t) { return 0.5 - 0.5 * std::cos(kPi * t); }

wxColour MixColours(const wxColour &a, const wxColour &b, double t) {
  t = std::clamp(t, 0.0, 1.0);
  const auto mix = [t](unsigned char x, unsigned char y) {
    return static_cast<unsigned char>(std::lround(x + (y - x) * t));
  };
  return wxColour(mix(a.Red(), b.Red()), mix(a.Green(), b.Green()),
                  mix(a.Blue(), b.Blue()));
}

// How much of the edge colour a comet has, from its tail (0) to its head (1):
// the edge colour at both ends, the track colour in the middle.
double CometEdgeWeight(double u) {
  return std::min(1.0,
                  (1.0 - SmoothStep(0.0, 0.35, u)) + SmoothStep(0.6, 0.9, u));
}

// The brightness of a comet, from its tail (0) to its head (1): it rises slowly
// along the tail, and has a short soft edge at the head.
double CometProfile(double u) {
  return SmoothStep(0.0, 0.7, u) * (1.0 - SmoothStep(0.9, 1.0, u));
}

} // namespace

Gauge::Gauge(wxWindow *parent, wxWindowID id, int range, const wxPoint &pos,
             const wxSize &size, long style, const wxString &name)
    : wxGLCanvas(parent, GaugeGLAttributes(), id, pos, size, style, name),
      m_range{std::max(range, 0)} {
  // The GL canvas does its own buffer swap, like the terminal: it does not set
  // wxBG_STYLE_PAINT. The context is made on the first paint, when the canvas
  // is shown (the gauge is hidden until there is progress).
  Bind(wxEVT_PAINT, &Gauge::OnPaint, this);
  Bind(wxEVT_SIZE, &Gauge::OnSize, this);
  m_animTimer.SetOwner(this);
  Bind(wxEVT_TIMER, &Gauge::OnAnimTimer, this, m_animTimer.GetId());
  Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent &) {});
}

Gauge::~Gauge() {
  m_animTimer.Stop();
  // Release the GL resources while the context is current. If it cannot be made
  // current (the canvas is hidden), the context of another control is current:
  // do not delete anything there. The driver frees our objects with the
  // context.
  if (m_glContext && m_glInitialized) {
    if (m_glContext->SetCurrent(*this)) {
      m_renderer.Destroy();
      TerminalGLRenderer::ReleaseContext();
    }
  }
}

void Gauge::ShowValueNow() {
  m_shownValue = m_value;
  m_completeMs = 0.0;
  m_fade = 1.0;
  m_animTimer.Stop();
}

void Gauge::SetRange(int range) {
  range = std::max(range, 0);
  if (range == m_range && !m_pulsing) {
    return;
  }
  m_range = range;
  m_value = std::min(m_value, m_range);
  m_pulsing = false;
  ShowValueNow();
  Refresh(false);
}

void Gauge::SetValue(int value) {
  value = std::clamp(value, 0, m_range);
  if (value == m_value && !m_pulsing) {
    return;
  }
  const bool wasPulsing = m_pulsing;
  m_value = value;
  m_pulsing = false;
  if (wasPulsing || m_value < m_shownValue) {
    // A new start, or a step back: show it at once.
    ShowValueNow();
    Refresh(false);
    return;
  }
  // Move to the new value, and fade out if it is the end.
  m_completeMs = 0.0;
  m_fade = 1.0;
  StartAnimation();
  Refresh(false);
}

void Gauge::StartAnimation() {
  if (!m_animTimer.IsRunning()) {
    m_lastAnim = std::chrono::steady_clock::now();
    m_animTimer.Start(kAnimIntervalMs);
  }
}

void Gauge::OnAnimTimer(wxTimerEvent &) {
  const auto now = std::chrono::steady_clock::now();
  // A long gap (the system was busy) must not make a jump.
  const double dt = std::min(
      std::chrono::duration<double, std::milli>(now - m_lastAnim).count(),
      100.0);
  m_lastAnim = now;

  bool busy = false;
  const double target = m_value;
  if (m_shownValue != target) {
    const double epsilon = std::max(m_range * 0.001, 0.001);
    m_shownValue += (target - m_shownValue) * (1.0 - std::exp(-dt / kFollowMs));
    if (std::fabs(target - m_shownValue) < epsilon) {
      m_shownValue = target;
    }
    busy = true;
  }
  if (m_shownValue == target && m_range > 0 && m_value == m_range) {
    // Full: stay for a moment, then fade out.
    m_completeMs += dt;
    m_fade = 1.0 - std::clamp((m_completeMs - kHoldMs) / kFadeMs, 0.0, 1.0);
    busy = busy || m_fade > 0.0;
  }
  if (!busy) {
    m_animTimer.Stop();
  }
  Refresh(false);
}

void Gauge::Pulse() {
  // The bar moves by the time since the last Pulse(), so its speed does not
  // depend on how often Pulse() is called.
  const auto now = std::chrono::steady_clock::now();
  if (m_pulsing) {
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(now - m_lastPulse).count();
    // One round trip (there and back) is the phase 0 to 1.
    m_pulsePhase =
        std::fmod(m_pulsePhase + elapsedMs / (2.0 * kPulseSweepMs), 1.0);
  } else {
    // The normal mode animations end: the bar is visible again.
    m_animTimer.Stop();
    m_fade = 1.0;
    m_completeMs = 0.0;
  }
  m_lastPulse = now;
  m_pulsing = true;
  Refresh(false);
}

void Gauge::SetTrackColour(const wxColour &colour) {
  m_trackColour = colour;
  Refresh(false);
}

void Gauge::SetEdgeColour(const wxColour &colour) {
  m_edgeColour = colour;
  Refresh(false);
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
  if (!IsShownOnScreen() || size.x <= 0 || size.y <= 0) {
    return;
  }
  if (!m_glContext) {
    // The same core profile context as the terminal.
    wxGLContextAttrs ctxAttrs;
    ctxAttrs.PlatformDefaults().CoreProfile().OGLVersion(3, 2).EndList();
    m_glContext = std::make_unique<wxGLContext>(this, nullptr, &ctxAttrs);
  }
  if (!m_glContext->IsOK() || !m_glContext->SetCurrent(*this)) {
    return;
  }
  if (!m_glInitialized) {
    // The gauge draws only solid quads: no glyph atlas.
    m_glInitialized = m_renderer.Init(false);
    if (!m_glInitialized) {
      return;
    }
  }

  // The track runs along the main axis: from the left (horizontal) or from the
  // bottom (vertical).
  const bool vertical = IsVertical();
  const int length = vertical ? size.y : size.x;

#ifdef __WXMSW__
  const double scale = 1.0;
#else
  const double scale = GetDPIScaleFactor();
#endif
  // The background clears the whole frame; the track is drawn on top of it.
  m_renderer.BeginFrame(size.x, size.y, scale, GetBackgroundColour());

  // Adds the part of the track from @p from to @p to (along the main axis).
  const auto addTrack = [&](int from, int to, const wxColour &colour,
                            double alpha) {
    from = std::clamp(from, 0, length);
    to = std::clamp(to, 0, length);
    if (to <= from) {
      return;
    }
    if (vertical) {
      m_renderer.AddSolidRect(0, size.y - to, size.x, to - from, colour,
                              static_cast<float>(alpha));
    } else {
      m_renderer.AddSolidRect(from, 0, to - from, size.y, colour,
                              static_cast<float>(alpha));
    }
  };
  // Without an edge colour the track has one colour.
  const wxColour edgeColour =
      m_edgeColour.IsOk() ? m_edgeColour : m_trackColour;

  if (m_pulsing) {
    // One comet runs from one end to the other and back (ping-pong). At each
    // end it leaves the bar completely, and then comes back from that end in
    // the other direction. It is made of thin slices with different
    // brightness and colour, and its head is the side it moves to.
    const int segment = std::min(std::max(length / 4, FromDIP(24)), length);
    const bool back = m_pulsePhase >= 0.5;
    const double t = back ? 2.0 - 2.0 * m_pulsePhase : 2.0 * m_pulsePhase;
    const double from = EaseInOut(t) * (length + segment) - segment;
    const int slices = std::clamp(segment / 2, 8, 64);
    for (int i = 0; i < slices; ++i) {
      // The slice number from the tail to the head.
      const int n = back ? slices - 1 - i : i;
      const double u = (n + 0.5) / slices;
      const int a =
          static_cast<int>(std::lround(from + segment * (double)i / slices));
      const int b = static_cast<int>(
          std::lround(from + segment * (double)(i + 1) / slices));
      addTrack(a, b, MixColours(m_trackColour, edgeColour, CometEdgeWeight(u)),
               CometProfile(u));
    }
  } else if (m_range > 0) {
    // The filled bar: the track colour, and the edge colour blended in at the
    // leading end.
    const int end =
        static_cast<int>(std::lround(length * m_shownValue / m_range));
    const int edge = std::min(end, FromDIP(28));
    addTrack(0, end - edge, m_trackColour, m_fade);
    const int slices = std::clamp(edge / 2, 1, 16);
    for (int i = 0; i < slices; ++i) {
      const double u = (i + 0.5) / slices; // 0 at the track, 1 at the end
      const int a =
          end - edge + static_cast<int>(std::lround(edge * (double)i / slices));
      const int b =
          end - edge +
          static_cast<int>(std::lround(edge * (double)(i + 1) / slices));
      addTrack(a, b, MixColours(m_trackColour, edgeColour, SmoothStep(0, 1, u)),
               m_fade);
    }
  }
  m_renderer.EndFrame();
  SwapBuffers();
}

#endif // USE_OPENGL
