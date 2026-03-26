#include "terminal_logger.h"

#include <wx/datetime.h>
#include <wx/stdpaths.h>

namespace {
wxString LevelToString(TerminalLogLevel level) {
  switch (level) {
  case TRACE:
    return "TRACE";
  case DEBUG:
    return "DEBUG";
  case WARN:
    return "WARN";
  case ERROR:
    return "ERROR";
  }
  return "?";
}
} // namespace

TerminalLogger &TerminalLogger::Get() {
  static TerminalLogger instance;
  return instance;
}

TerminalLogger::TerminalLogger() {
  wxString dir = wxFileName::GetHomeDir() + wxFileName::GetPathSeparator() +
                 ".wxTerminalEmulator";
  if (!wxFileName::DirExists(dir))
    wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  m_logPath = dir + wxFileName::GetPathSeparator() + "trace.log";
  m_file.Open(m_logPath, "a");
}

void TerminalLogger::SetLogFile(const wxString &path) {
  if (m_file.IsOpened())
    m_file.Close();
  m_logPath = path;
  m_file.Open(m_logPath, "a");
}

void TerminalLogger::Write(TerminalLogLevel level, const wxString &msg) {
  if (!m_file.IsOpened())
    return;
  wxString line = wxDateTime::Now().FormatISOCombined(' ') + " [" +
                  LevelToString(level) + "] " + msg + "\n";
  m_file.Write(line);
  m_file.Flush();
}

TerminalLogger::LogEntry TerminalLogger::Log(TerminalLogLevel level) {
  return LogEntry(level, level >= m_level);
}

// LogEntry implementation

TerminalLogger::LogEntry::LogEntry(TerminalLogLevel level, bool enabled)
    : m_level(level), m_enabled(enabled) {}

TerminalLogger::LogEntry::~LogEntry() {
  if (m_enabled && !m_buf.empty())
    TerminalLogger::Get().Write(m_level, m_buf);
}

TerminalLogger::LogEntry &
TerminalLogger::LogEntry::operator<<(const wxString &s) {
  if (m_enabled)
    m_buf += s;
  return *this;
}

TerminalLogger::LogEntry &TerminalLogger::LogEntry::operator<<(const char *s) {
  if (m_enabled)
    m_buf += s;
  return *this;
}

TerminalLogger::LogEntry &
TerminalLogger::LogEntry::operator<<(const std::string &s) {
  if (m_enabled)
    m_buf += s;
  return *this;
}

TerminalLogger::LogEntry &TerminalLogger::LogEntry::operator<<(int v) {
  if (m_enabled)
    m_buf << v;
  return *this;
}

TerminalLogger::LogEntry &TerminalLogger::LogEntry::operator<<(long v) {
  if (m_enabled)
    m_buf << v;
  return *this;
}

TerminalLogger::LogEntry &TerminalLogger::LogEntry::operator<<(long long v) {
  if (m_enabled)
    m_buf << v;
  return *this;
}

TerminalLogger::LogEntry &TerminalLogger::LogEntry::operator<<(unsigned int v) {
  if (m_enabled)
    m_buf << v;
  return *this;
}

TerminalLogger::LogEntry &
TerminalLogger::LogEntry::operator<<(unsigned long v) {
  if (m_enabled)
    m_buf << v;
  return *this;
}

TerminalLogger::LogEntry &TerminalLogger::LogEntry::operator<<(double v) {
  if (m_enabled)
    m_buf += wxString::Format("%.6g", v);
  return *this;
}

TerminalLogger::LogEntry &
TerminalLogger::LogEntry::operator<<(const wxArrayString &arr) {
  if (m_enabled) {
    m_buf += "[";
    for (size_t i = 0; i < arr.size(); ++i) {
      if (i > 0)
        m_buf += ", ";
      m_buf += arr[i];
    }
    m_buf += "]";
  }
  return *this;
}

TerminalLogger::LogEntry &
TerminalLogger::LogEntry::operator<<(const std::vector<wxString> &arr) {
  if (m_enabled) {
    m_buf += "[";
    for (size_t i = 0; i < arr.size(); ++i) {
      if (i > 0)
        m_buf += ", ";
      m_buf += arr[i];
    }
    m_buf += "]";
  }
  return *this;
}

TerminalLogger::LogEntry &
TerminalLogger::LogEntry::operator<<(const std::vector<std::string> &arr) {
  if (m_enabled) {
    m_buf += "[";
    for (size_t i = 0; i < arr.size(); ++i) {
      if (i > 0)
        m_buf += ", ";
      m_buf += arr[i];
    }
    m_buf += "]";
  }
  return *this;
}

TerminalLogger::LogEntry &
TerminalLogger::LogEntry::operator<<(std::ostream &(*)(std::ostream &)) {
  // endl triggers flush — the destructor handles writing
  return *this;
}
