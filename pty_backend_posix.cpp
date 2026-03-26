#include "pty_backend_posix.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

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

std::unique_ptr<PtyBackend> PtyBackend::Create() {
  return std::make_unique<PosixPtyBackend>();
}

PosixPtyBackend::PosixPtyBackend() = default;
PosixPtyBackend::~PosixPtyBackend() { Stop(); }

bool PosixPtyBackend::Start(const std::string &command,
                            OutputCallback on_output) {
  Stop();
  m_onOutput = std::move(on_output);

  struct winsize ws {};
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
    const char *shell = command.empty() ? nullptr : command.c_str();
    if (!shell) {
      shell = getenv("SHELL");
      if (!shell)
        shell = "/bin/sh";
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
  m_thread = std::thread([this] { ReaderThread(); });
  return true;
}

void PosixPtyBackend::Write(const std::string &data) {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_writeBuffer.insert(m_writeBuffer.end(), data.begin(), data.end());
  }
  m_cv.notify_one();
}

void PosixPtyBackend::Resize(int cols, int rows) {
  if (m_masterFd < 0)
    return;
  struct winsize ws {};
  ws.ws_col = static_cast<unsigned short>(cols);
  ws.ws_row = static_cast<unsigned short>(rows);
  ioctl(m_masterFd, TIOCSWINSZ, &ws);
}

void PosixPtyBackend::Stop() {
  m_running = false;
  m_cv.notify_all();

  if (m_thread.joinable())
    m_thread.join();

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
    // Drain outbound write buffer
    std::vector<char> pending;
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_cv.wait_for(lock, std::chrono::milliseconds(1),
                    [this] { return !m_running || !m_writeBuffer.empty(); });
      if (!m_writeBuffer.empty())
        pending.swap(m_writeBuffer);
    }

    if (!pending.empty() && m_masterFd >= 0) {
      const char *p = pending.data();
      size_t remaining = pending.size();
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

    if (m_masterFd >= 0) {
      struct pollfd pfd {};
      pfd.fd = m_masterFd;
      pfd.events = POLLIN;

      int ret = poll(&pfd, 1, 1); // 1ms timeout
      if (ret > 0 && (pfd.revents & POLLIN)) {
        ssize_t n = read(m_masterFd, buf, sizeof(buf));
        if (n > 0) {
          if (m_onOutput)
            m_onOutput(std::string(buf, static_cast<size_t>(n)));
        } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
          m_running = false;
          break;
        }
      } else if (ret < 0 && errno != EINTR) {
        m_running = false;
        break;
      }
    }
  }
}

} // namespace terminal
