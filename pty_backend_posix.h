#pragma once

#include "pty_backend.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace terminal {

class PosixPtyBackend final : public PtyBackend {
public:
  PosixPtyBackend();
  ~PosixPtyBackend() override;

  bool Start(const std::string &command, OutputCallback on_output) override;
  void Write(const std::string &data) override;
  void Resize(int cols, int rows) override;
  void Stop() override;

private:
  void ReaderThread();
  void WriterThread();

  std::atomic<bool> m_running{false};
  std::thread m_readerThread;
  std::thread m_writerThread;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::vector<char> m_writeBuffer;
  OutputCallback m_onOutput;

  int m_masterFd{-1};
  pid_t m_childPid{-1};
};

} // namespace terminal
