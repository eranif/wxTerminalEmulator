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
 * it needs 2 seconds from one end to the other, however often Pulse() is
 * called. SetValue() or SetRange() end the indeterminate mode.
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

protected:
  wxSize DoGetBestSize() const override;

private:
  void OnPaint(wxPaintEvent &event);
  void OnSize(wxSizeEvent &event);

  int m_range{100};
  int m_value{0};
  bool m_pulsing{false};
  double m_pulsePhase{0.0}; // 0..1, where the moving bar is
  std::chrono::steady_clock::time_point m_lastPulse;
  std::unique_ptr<wxGLContext> m_glContext;
  wxColour m_trackColour;
  TerminalGLRenderer m_renderer;
  bool m_glInitialized{false};
};

#else

class Gauge : public wxGauge {
public:
  void SetTrackColour(const wxColour &colour) { wxUnusedVar(colour); }
};

#endif // USE_OPENGL
