#pragma once

// The progress bar of wxTerminalViewCtrl. Without OpenGL it is the native
// wxGauge. With OpenGL (USE_OPENGL=1) it is a class with the same basic API
// that draws itself with OpenGL, like the terminal does.
#include "terminal_gl_renderer.h" // defines USE_OPENGL when it is not set
#include <wx/gauge.h>

#if USE_OPENGL

#include <chrono>
#include <memory>
#include <wx/glcanvas.h>
#include <wx/timer.h>

/**
 * @brief A thin progress bar drawn with OpenGL.
 *
 * It has the basic API of wxGauge: SetRange(), GetRange(), SetValue(),
 * GetValue(), Pulse() and IsVertical(). Create it with the same arguments as a
 * wxGauge (the style is wxGA_HORIZONTAL or wxGA_VERTICAL).
 *
 * The gauge has two parts. The track is the part that fills (or moves): it is
 * drawn with the colour of SetTrackColour(). The background is everything
 * behind the track: it is drawn with GetBackgroundColour(), and the whole
 * gauge is cleared with it before the track is drawn.
 *
 * Pulse() puts the gauge in indeterminate mode and moves the track, like
 * wxGauge does on GTK and macOS: call it again and again (for example from a
 * wxTimer) to animate it. The track moves by the time since the last Pulse():
 * it needs 1.4 seconds from one end to the other, however often Pulse() is
 * called. SetValue() or SetRange() end the indeterminate mode.
 *
 * The indeterminate track is one comet with soft edges that runs from one end
 * to the other and back (ping-pong). It moves slowly near the ends and fast in
 * the middle, and its head is the side it moves to.
 *
 * In the normal mode the bar moves smoothly to a new value that is higher than
 * the shown one (the gauge has its own timer for it). A lower value is shown at
 * once. When the value reaches the range, the bar stays for a moment and then
 * fades out; SetValue() with another value or Pulse() shows it again.
 */
class Gauge : public wxGLCanvas {
public:
  Gauge(wxWindow *parent, wxWindowID id, int range,
        const wxPoint &pos = wxDefaultPosition,
        const wxSize &size = wxDefaultSize, long style = wxGA_HORIZONTAL,
        const wxString &name = "gauge");
  ~Gauge() override;

  Gauge(const Gauge &) = delete;
  Gauge &operator=(const Gauge &) = delete;

  void SetRange(int range);
  int GetRange() const { return m_range; }
  /// Set the position, 0 to GetRange(). Ends the indeterminate mode.
  void SetValue(int value);
  int GetValue() const { return m_value; }
  /// Switch to the indeterminate mode and move the bar (see above).
  void Pulse();
  bool IsVertical() const { return HasFlag(wxGA_VERTICAL); }

  // A gauge never takes the keyboard focus from the terminal.
  bool AcceptsFocus() const override { return false; }

  /// The colour of the track: the part that fills (or moves). The background
  /// behind it is set with SetBackgroundColour().
  void SetTrackColour(const wxColour &colour);
  /// The colour of the edges of the track: the tail and the head of the moving
  /// bar, and the leading end of the filled bar. It blends into the track
  /// colour towards the middle. Not set (or the same as the track colour): the
  /// track has one colour.
  void SetEdgeColour(const wxColour &colour);

protected:
  wxSize DoGetBestSize() const override;

private:
  void OnPaint(wxPaintEvent &event);
  void OnSize(wxSizeEvent &event);
  void OnAnimTimer(wxTimerEvent &event);
  void StartAnimation();
  /// Show the bar at once at the value, without any animation.
  void ShowValueNow();

  int m_range{100};
  int m_value{0};
  bool m_pulsing{false};
  double m_pulsePhase{0.0}; // 0..1, one round trip of the comet
  std::chrono::steady_clock::time_point m_lastPulse;

  // The animation of the normal mode: the shown value follows m_value.
  double m_shownValue{0.0};
  double m_completeMs{0.0}; // how long the bar has been full
  double m_fade{1.0};       // 1: visible, 0: faded out
  wxTimer m_animTimer;
  std::chrono::steady_clock::time_point m_lastAnim;
  std::unique_ptr<wxGLContext> m_glContext;
  wxColour m_trackColour;
  wxColour m_edgeColour;
  TerminalGLRenderer m_renderer;
  bool m_glInitialized{false};
};

#else

class Gauge : public wxGauge {
public:
  using wxGauge::wxGauge;
  void SetTrackColour(const wxColour &colour) { wxUnusedVar(colour); }
  void SetEdgeColour(const wxColour &colour) { wxUnusedVar(colour); }
};

#endif // USE_OPENGL
