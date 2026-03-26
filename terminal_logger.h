#pragma once

#include <wx/arrstr.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/string.h>

#include <vector>

enum TerminalLogLevel { TRACE = 0, DEBUG = 1, WARN = 2, ERROR = 3 };

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

    LogEntry &operator<<(const wxString &s);
    LogEntry &operator<<(const char *s);
    LogEntry &operator<<(const std::string &s);
    LogEntry &operator<<(int v);
    LogEntry &operator<<(long v);
    LogEntry &operator<<(long long v);
    LogEntry &operator<<(unsigned int v);
    LogEntry &operator<<(unsigned long v);
    LogEntry &operator<<(double v);
    LogEntry &operator<<(const wxArrayString &arr);
    LogEntry &operator<<(const std::vector<wxString> &arr);
    LogEntry &operator<<(const std::vector<std::string> &arr);

    // Support for std::endl
    LogEntry &operator<<(std::ostream &(*)(std::ostream &));

  private:
    wxString m_buf;
    TerminalLogLevel m_level;
    bool m_enabled;
  };

  LogEntry Log(TerminalLogLevel level);

private:
  TerminalLogger();
  void Write(TerminalLogLevel level, const wxString &msg);

  TerminalLogLevel m_level{DEBUG};
  wxString m_logPath;
  wxFFile m_file;
};

#define LOG(level) TerminalLogger::Get().Log(level)
