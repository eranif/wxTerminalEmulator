#pragma once

#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>
#include <wx/arrstr.h>
#include <wx/ffile.h>
#include <wx/filename.h>
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
