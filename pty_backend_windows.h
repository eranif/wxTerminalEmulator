#pragma once

#include "pty_backend.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include <windows.h>

namespace terminal {

class WindowsPtyBackend final : public PtyBackend {
public:
  WindowsPtyBackend();
  ~WindowsPtyBackend() override;

  bool Start(const std::string &command, OutputCallback on_output) override;
  void Write(const std::string &data) override;
  void Resize(int cols, int rows) override;
  void Stop() override;

private:
  void ReaderThread();
  bool CreateConPty(const std::string &command);
  void DestroyConPty();

  std::atomic<bool> m_running{false};
  std::thread m_thread;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::vector<char> m_writeBuffer;
  OutputCallback m_onOutput;

  HANDLE m_hInputWrite{nullptr};
  HANDLE m_hOutputRead{nullptr};
  HANDLE m_hProcess{nullptr};
  HANDLE m_hThread{nullptr};
  HPCON m_hPC{nullptr};
};

} // namespace terminal