#include "pty_backend_posix.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "terminal_event.h"
#include "terminal_logger.h"
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

namespace terminal {

std::unique_ptr<PtyBackend> PtyBackend::Create(wxEvtHandler *handler) {
  return std::make_unique<PosixPtyBackend>(handler);
}

PosixPtyBackend::PosixPtyBackend(wxEvtHandler *handler) : PtyBackend(handler) {}
PosixPtyBackend::~PosixPtyBackend() { Stop(); }

bool PosixPtyBackend::Start(const std::string &command,
                            const std::optional<EnvironmentList> &environment,
                            OutputCallback on_output) {
  Stop();
  m_onOutput = std::move(on_output);

  struct winsize ws{};
  ws.ws_col = 120;
  ws.ws_row = 30;

  pid_t pid = forkpty(&m_masterFd, nullptr, nullptr, &ws);
  if (pid < 0) {
    if (m_onOutput)
      m_onOutput("[forkpty failed: " + std::string(strerror(errno)) + "]\r\n");
    return false;
  }

  if (pid == 0) {
    // Child process
    if (environment.has_value() && !environment->empty()) {
      clearenv();
      for (const auto &entry : *environment) {
        const auto pos = entry.find('=');
        if (pos == std::string::npos)
          continue;
        std::string key = entry.substr(0, pos);
        std::string value = entry.substr(pos + 1);
        setenv(key.c_str(), value.c_str(), 1);
      }
    }

    // Ensure TERM exists even with inherited or explicit env.
    const char *shell = command.empty() ? nullptr : command.c_str();
    if (!shell) {
      shell = getenv("SHELL");
      if (!shell) {
        shell = "/bin/sh";
      }
    }
    setenv("TERM", "xterm-256color", 1);
    execlp(shell, shell, nullptr);
    _exit(127);
  }

  // Parent
  m_childPid = pid;

  // Set primary fd to non-blocking
  int flags = fcntl(m_masterFd, F_GETFL);
  if (flags != -1)
    fcntl(m_masterFd, F_SETFL, flags | O_NONBLOCK);

  m_running = true;
  m_readerThread = std::thread([this] { ReaderThread(); });
  m_writerThread = std::thread([this] { WriterThread(); });
  return true;
}

void PosixPtyBackend::Write(const std::string &data) {
  TLOG_IF_TRACE { TLOG_TRACE() << "Sending: " << data << std::endl; }
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_writeBuffer.insert(m_writeBuffer.end(), data.begin(), data.end());
  }
  m_cv.notify_one();
  TLOG_IF_TRACE { TLOG_TRACE() << "NotifyOne is called" << data << std::endl; }
}

void PosixPtyBackend::Resize(int cols, int rows) {
  if (m_masterFd < 0)
    return;
  struct winsize ws{};
  ws.ws_col = static_cast<unsigned short>(cols);
  ws.ws_row = static_cast<unsigned short>(rows);
  ioctl(m_masterFd, TIOCSWINSZ, &ws);
}

void PosixPtyBackend::SendBreak() {
  // On POSIX systems, sending '\x03' (Ctrl-C) works correctly
  // because the PTY layer translates it to SIGINT for the child process
  Write("\x03");
}

void PosixPtyBackend::Stop() {
  m_running = false;
  m_cv.notify_all();

  if (m_readerThread.joinable())
    m_readerThread.join();
  if (m_writerThread.joinable())
    m_writerThread.join();

  if (m_masterFd >= 0) {
    close(m_masterFd);
    m_masterFd = -1;
  }

  if (m_childPid > 0) {
    kill(m_childPid, SIGHUP);
    int status = 0;
    waitpid(m_childPid, &status, WNOHANG);
    m_childPid = -1;
  }
}

void PosixPtyBackend::ReaderThread() {
  char buf[4096];

  while (m_running) {
    if (m_masterFd < 0)
      break;

    struct pollfd pfd{};
    pfd.fd = m_masterFd;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, 50);
    if (ret > 0 && (pfd.revents & POLLIN)) {
      std::string accumulated;
      for (int i = 0; i < 5; ++i) {
        ssize_t n = read(m_masterFd, buf, sizeof(buf));
        if (n > 0) {
          accumulated.append(buf, static_cast<size_t>(n));
        } else if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
          break;
        } else {
          m_running = false;
          break;
        }
      }
      if (!accumulated.empty() && m_onOutput) {
        TLOG_IF_TRACE {
          TLOG_TRACE() << "Terminal output read: " << accumulated << std::endl;
        }
        m_onOutput(accumulated);
      }
      if (!m_running)
        break;
    } else if (ret < 0 && errno != EINTR) {
      m_running = false;
      break;
    } else if (ret > 0 && (pfd.revents & (POLLHUP | POLLERR | POLLNVAL))) {
      wxTerminalEvent terminate_event{wxEVT_TERMINAL_TERMINATED};
      m_eventHandler->AddPendingEvent(terminate_event);
      m_running = false;
      break;
    }
  }
  TLOG_DEBUG() << "Going down" << std::endl;
}

void PosixPtyBackend::WriterThread() {
  while (m_running) {
    std::vector<char> pending;
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_cv.wait(lock, [this] { return !m_running || !m_writeBuffer.empty(); });
      if (!m_running && m_writeBuffer.empty())
        break;
      pending.swap(m_writeBuffer);
    }

    if (!pending.empty() && m_masterFd >= 0) {
      const char *p = pending.data();
      size_t remaining = pending.size();
      TLOG_IF_TRACE {
        TLOG_TRACE() << "Writing data to child process: " << remaining
                     << " bytes" << std::endl;
      }
      while (remaining > 0) {
        ssize_t n = write(m_masterFd, p, remaining);
        if (n > 0) {
          p += n;
          remaining -= static_cast<size_t>(n);
        } else if (n < 0 && errno != EAGAIN && errno != EINTR) {
          break;
        }
      }
    }
  }
}

} // namespace terminal
