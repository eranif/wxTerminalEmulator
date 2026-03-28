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

enum class TerminalLogLevel { kTrace = 0, kDebug = 1, kWarn = 2, kError = 3 };

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

  TerminalLogLevel m_level{TerminalLogLevel::kDebug};
  wxString m_logPath;
  wxFFile m_file;
};

#define LOG(level) TerminalLogger::Get().Log(level)
#define LOG_DEBUG() LOG(TerminalLogLevel::kDebug)
#define LOG_WARN() LOG(TerminalLogLevel::kWarn)
#define LOG_ERROR() LOG(TerminalLogLevel::kError)
#define LOG_TRACE() LOG(TerminalLogLevel::kTrace)

struct LogFunction {
  wxString function_name;
  wxStopWatch sw;
  std::array<size_t, 10> counters{};
  std::array<wxString, 10> counter_names{};
  size_t next_counter{0};

  /**
   * @brief Starts timing a function scope for later debug logging.
   *
   * The timer begins immediately and the supplied function name is stored so
   * the destructor can report which function completed.
   *
   * @param funcname Name of the function/scope being measured.
   */
  LogFunction(const wxString &funcname) : function_name{funcname} {
    sw.Start();
  }

  /**
   * @brief Emits the elapsed time and any registered counters.
   *
   * When the object goes out of scope, it logs the total execution time and
   * then logs each counter added through AddCounter().
   */
  ~LogFunction() {
    LOG_DEBUG() << "Function: " << function_name
                << " completed in:" << sw.TimeInMicro().ToLong() << std::endl;
    // Print counters
    for (size_t i = 0; i < next_counter; ++i) {
      LOG_DEBUG() << counter_names[i] << ": " << counters[i] << std::endl;
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
    if (next_counter >= 10) {
      static size_t null_counter{0};
      return null_counter;
    }
    counter_names[next_counter] = name;
    size_t &counter = counters[next_counter];
    next_counter++;
    return counter;
  }
};
