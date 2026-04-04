#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <wx/event.h>

namespace terminal {

class PtyBackend {
public:
  PtyBackend(wxEvtHandler *eventHandler) : m_eventHandler{eventHandler} {}
  using OutputCallback = std::function<void(const std::string &)>;
  using EnvironmentList = std::vector<std::string>;

  virtual ~PtyBackend() = default;
  virtual bool Start(const std::string &command,
                     const std::optional<EnvironmentList> &environment,
                     OutputCallback on_output) = 0;
  virtual void Write(const std::string &data) = 0;
  virtual void Resize(int cols, int rows) = 0;
  virtual void SendBreak() = 0; // Ctrl-C
  virtual void Stop() = 0;
  virtual bool IsBash() = 0;
  static std::unique_ptr<PtyBackend> Create(wxEvtHandler *handler);

protected:
  wxEvtHandler *m_eventHandler{nullptr};
};

} // namespace terminal
