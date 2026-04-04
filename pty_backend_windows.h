#pragma once

#include "pty_backend.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <windows.h>

namespace terminal {

class WindowsPtyBackend final : public PtyBackend {
public:
  WindowsPtyBackend(wxEvtHandler *eventHandler);
  ~WindowsPtyBackend() override;

  bool Start(const std::string &command,
             const std::optional<EnvironmentList> &environment,
             OutputCallback on_output) override;
  void Write(const std::string &data) override;
  void Resize(int cols, int rows) override;
  void SendBreak() override;
  void Stop() override;

  wxArrayString GetChildren() const override;

  /**
   * @brief Metadata for a process collected from the Windows process snapshot.
   */
  struct ProcessInfo {
    /** @brief Process identifier. */
    DWORD pid{0};

    /** @brief Executable name or image name. */
    std::wstring imageName;

    /** @brief Creation timestamp used for sorting by age. */
    FILETIME creation_time;
  };

private:
  void ReaderThread();
  void WriterThread();
  void InterruptIo();
  bool CreateConPty(const std::string &command,
                    const std::optional<EnvironmentList> &environment);
  void DestroyConPty();

  std::atomic<bool> m_running{false};
  std::thread m_readerThread;
  std::thread m_writerThread;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::vector<char> m_writeBuffer;
  OutputCallback m_onOutput;

  HANDLE m_hInputWrite{nullptr};
  HANDLE m_hOutputRead{nullptr};
  HANDLE m_hProcess{nullptr};
  long m_processPid{-1};
  HANDLE m_hThread{nullptr};
  HPCON m_hPC{nullptr};
};

} // namespace terminal