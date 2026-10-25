#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <wx/arrstr.h>
#include <wx/event.h>

namespace terminal {

class PtyBackend {
public:
  PtyBackend(wxEvtHandler *eventHandler) : m_eventHandler{eventHandler} {}
  // Called on the I/O thread with the output of the child. It may block until
  // the view has room for more (back-pressure), so do not call it while
  // holding a lock that Stop() or the GUI thread needs.
  using OutputCallback = std::function<void(const std::string &)>;
  using EnvironmentList = std::vector<std::string>;

  virtual ~PtyBackend() = default;
  virtual bool Start(const std::string &command,
                     const std::optional<EnvironmentList> &environment,
                     const std::optional<std::string> &workingDirectory,
                     OutputCallback on_output) = 0;
  virtual void Write(const std::string &data) = 0;
  virtual void Resize(int cols, int rows) = 0;
  virtual void SendBreak() = 0; // Ctrl-C
  virtual void Stop() = 0;
  /**
   * @brief Returns the image names of the PTY process and its direct children.
   * newest processes are placed first.
   */
  virtual wxArrayString GetChildren() const = 0;
  static std::unique_ptr<PtyBackend> Create(wxEvtHandler *handler);

protected:
  wxEvtHandler *m_eventHandler{nullptr};
};

} // namespace terminal
