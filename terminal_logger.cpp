#include "terminal_logger.h"

#include <wx/datetime.h>

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

TerminalLogger::TerminalLogger() {}

void TerminalLogger::EnsureOpen() {
  if (m_file.IsOpened())
    return;
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
  EnsureOpen();
  if (!m_file.IsOpened())
    return;
  wxDateTime now = wxDateTime::UNow();
  wxString line = now.FormatISOCombined(' ') +
                  wxString::Format(".%03ld", now.GetMillisecond()) + " [" +
                  LevelToString(level) + "] " + msg + "\n";
  m_file.Write(line);
  m_file.Flush();
}

TerminalLogger::LogEntry TerminalLogger::Log(TerminalLogLevel level) {
  return LogEntry(level, level >= m_level);
}

TerminalLogger::LogEntry::LogEntry(TerminalLogLevel level, bool enabled)
    : m_level(level), m_enabled(enabled) {}

TerminalLogger::LogEntry::~LogEntry() {
  if (m_enabled) {
    std::string s = m_ss.str();
    if (!s.empty())
      TerminalLogger::Get().Write(m_level, wxString::FromUTF8(s));
  }
}

TerminalLogger::LogEntry &
TerminalLogger::LogEntry::operator<<(const wxString &s) {
  if (m_enabled)
    m_ss << s.ToStdString(wxConvUTF8);
  return *this;
}

TerminalLogger::LogEntry &
TerminalLogger::LogEntry::operator<<(const wxArrayString &arr) {
  if (m_enabled) {
    m_ss << "[";
    for (size_t i = 0; i < arr.size(); ++i) {
      if (i > 0)
        m_ss << ", ";
      m_ss << arr[i].ToStdString(wxConvUTF8);
    }
    m_ss << "]";
  }
  return *this;
}

TerminalLogger::LogEntry &
TerminalLogger::LogEntry::operator<<(const std::vector<wxString> &arr) {
  if (m_enabled) {
    m_ss << "[";
    for (size_t i = 0; i < arr.size(); ++i) {
      if (i > 0)
        m_ss << ", ";
      m_ss << arr[i].ToStdString(wxConvUTF8);
    }
    m_ss << "]";
  }
  return *this;
}
