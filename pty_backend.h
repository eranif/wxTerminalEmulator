#pragma once

#include <functional>
#include <string>
#include <memory>

namespace terminal {

class PtyBackend {
public:
  using OutputCallback = std::function<void(const std::string&)>;

  virtual ~PtyBackend() = default;
  virtual bool Start(const std::string& command, OutputCallback on_output) = 0;
  virtual void Write(const std::string& data) = 0;
  virtual void Resize(int cols, int rows) = 0;
  virtual void Stop() = 0;

  static std::unique_ptr<PtyBackend> Create();
};

} // namespace terminal
