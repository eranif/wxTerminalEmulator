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
             const std::optional<std::string> &workingDirectory,
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
  // Result of ReadOutput().
  enum class ReadResult { kData, kTimeout, kProcessExited, kStopped, kBroken };
  // Read once from the output pipe (at most buf.size() bytes) and pass the
  // data to m_onOutput. Waits up to timeoutMs for data, and returns earlier
  // when Stop() is called or, with watchProcess, when the process exits.
  // ov is the reader's OVERLAPPED; no read is pending when this returns.
  ReadResult ReadOutput(std::vector<char> &buf, OVERLAPPED &ov, DWORD timeoutMs,
                        bool watchProcess);
  void WriterThread();
  void InterruptIo();
  bool CreateConPty(const std::string &command,
                    const std::optional<EnvironmentList> &environment,
                    const std::optional<std::string> &workingDirectory);
  void DestroyConPty();

  std::atomic<bool> m_running{false};
  std::thread m_readerThread;
  std::thread m_writerThread;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  std::vector<char> m_writeBuffer;
  OutputCallback m_onOutput;

  HANDLE m_hInputWrite{nullptr};
  HANDLE m_hOutputRead{nullptr}; // overlapped: the reader waits on it
  HANDLE m_hStopEvent{nullptr};  // set by Stop() to wake the reader
  HANDLE m_hProcess{nullptr};
  long m_processPid{-1};
  HANDLE m_hThread{nullptr};
  HPCON m_hPC{nullptr};
};

} // namespace terminal