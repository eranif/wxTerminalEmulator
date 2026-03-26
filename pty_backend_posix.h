#pragma once

#include "pty_backend.h"

namespace terminal {

class PosixPtyBackend final : public PtyBackend {
public:
  bool Start(const std::string &command, OutputCallback on_output) override;
  void Write(const std::string &data) override;
  void Resize(int cols, int rows) override;
  void Stop() override;
};

} // namespace terminal
