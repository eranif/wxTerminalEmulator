#pragma once

#include "wx/gdicmn.h"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>
#include <wx/arrstr.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/stopwatch.h>
#include <wx/string.h>

enum class TerminalLogLevel {
  kTrace = 0,
  kDebug = 1,
  kInfo = 2,
  kWarn = 3,
  kError = 4,
};

class TerminalLogger {
public:
  static TerminalLogger &Get();

  void SetLevel(TerminalLogLevel level) { m_level = level; }
  TerminalLogLevel GetLevel() const { return m_level; }
  void SetLogFile(const wxString &path);

  class LogEntry {
  public:
    LogEntry(TerminalLogLevel level, bool enabled);
    ~LogEntry();

    // Generic: anything std::ostream supports (numbers, std::hex, etc.)
    template <typename T> LogEntry &operator<<(const T &v) {
      if (m_enabled)
        m_ss << v;
      return *this;
    }

    // Stream manipulators (std::hex, std::dec, std::endl, etc.)
    LogEntry &operator<<(std::ostream &(*manip)(std::ostream &)) {
      if (m_enabled)
        manip(m_ss);
      return *this;
    }

    LogEntry &operator<<(std::ios_base &(*manip)(std::ios_base &)) {
      if (m_enabled)
        manip(m_ss);
      return *this;
    }

    // wx-specific types
    LogEntry &operator<<(const wxString &s);
    LogEntry &operator<<(const wxRect &s);
    LogEntry &operator<<(const wxPoint &s);
    LogEntry &operator<<(const wxArrayString &arr);
    LogEntry &operator<<(const std::vector<wxString> &arr);
    LogEntry &operator<<(const std::vector<std::string> &arr);

  private:
    std::ostringstream m_ss;
    TerminalLogLevel m_level;
    bool m_enabled;
  };

  LogEntry Log(TerminalLogLevel level);

private:
  TerminalLogger();
  void Write(TerminalLogLevel level, const wxString &msg);
  void EnsureOpen();

  TerminalLogLevel m_level{TerminalLogLevel::kError};
  wxString m_logPath;
  wxFFile m_file;
};

#define TLOG(level) TerminalLogger::Get().Log(level)
#define TLOG_DEBUG() TLOG(TerminalLogLevel::kDebug)
#define TLOG_WARN() TLOG(TerminalLogLevel::kWarn)
#define TLOG_ERROR() TLOG(TerminalLogLevel::kError)
#define TLOG_TRACE() TLOG(TerminalLogLevel::kTrace)
#define TLOG_INFO() TLOG(TerminalLogLevel::kInfo)

#define TLOG_IF_TRACE                                                          \
  if (TerminalLogger::Get().GetLevel() <= TerminalLogLevel::kTrace)
#define TLOG_IF_DEBUG                                                          \
  if (TerminalLogger::Get().GetLevel() <= TerminalLogLevel::kDebug)

struct LogFunction {
  wxString function_name;
  std::unique_ptr<wxStopWatch> stop_watch_{nullptr};
  std::array<size_t, 10> counters{};
  std::array<wxString, 10> counter_names{};
  size_t next_counter{0};
  TerminalLogLevel log_level_{TerminalLogLevel::kTrace};

  /**
   * @brief Starts timing a function scope for later debug logging.
   *
   * The timer begins immediately and the supplied function name is stored so
   * the destructor can report which function completed.
   *
   * @param funcname Name of the function/scope being measured.
   */
  LogFunction(const wxString &funcname,
              TerminalLogLevel log_level = TerminalLogLevel::kTrace)
      : function_name{funcname}, log_level_{log_level} {
    if (TerminalLogger::Get().GetLevel() <= log_level_) {
      stop_watch_ = std::make_unique<wxStopWatch>();
      stop_watch_->Start();
    }
  }

  /**
   * @brief Emits the elapsed time and any registered counters.
   *
   * When the object goes out of scope, it logs the total execution time and
   * then logs each counter added through AddCounter().
   */
  ~LogFunction() {
    if (stop_watch_) {
      TLOG(log_level_) << "Function: " << function_name << " completed in:"
                       << stop_watch_->TimeInMicro().ToLong() << std::endl;
      // Print counters
      for (size_t i = 0; i < next_counter; ++i) {
        TLOG(log_level_) << "    > " << counter_names[i] << ": " << counters[i]
                         << std::endl;
      }
    }
  }

  /**
   * @brief Registers a named counter for the current scope.
   *
   * The returned reference can be incremented by the caller during the scope.
   * Up to 10 counters may be tracked. If the limit is exceeded, a dummy counter
   * is returned.
   *
   * @param name Human-readable label for the counter.
   * @return Reference to the stored counter value.
   */
  size_t &AddCounter(const wxString &name) {
    if (!stop_watch_ || next_counter >= 10) {
      static size_t null_counter{0};
      return null_counter;
    }
    counter_names[next_counter] = name;
    size_t &counter = counters[next_counter];
    next_counter++;
    return counter;
  }
};
