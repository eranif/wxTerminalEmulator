#include "pty_backend_posix.h"

#include <memory>

namespace terminal {

std::unique_ptr<PtyBackend> PtyBackend::Create() {
  return std::make_unique<PosixPtyBackend>();
}

bool PosixPtyBackend::Start(const std::string &command,
                            OutputCallback on_output) {
  (void)command;
  if (on_output)
    on_output("[POSIX PTY backend placeholder: forkpty implementation still "
              "pending]\n");
  return true;
}

void PosixPtyBackend::Write(const std::string &data) { (void)data; }
void PosixPtyBackend::Resize(int cols, int rows) {
  (void)cols;
  (void)rows;
}
void PosixPtyBackend::Stop() {}

} // namespace terminal
