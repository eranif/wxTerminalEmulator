#include "terminal_logger.h"
#include "wx/utils.h"

#include <wx/datetime.h>

namespace {
wxString LevelToString(TerminalLogLevel level) {
  switch (level) {
  case TerminalLogLevel::kTrace:
    return "TRACE";
  case TerminalLogLevel::kDebug:
    return "DEBUG";
  case TerminalLogLevel::kWarn:
    return "WARN";
  case TerminalLogLevel::kError:
    return "ERROR";
  case TerminalLogLevel::kInfo:
    return "INFO";
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
#ifdef __WXMSW__
  wxString dir;
  dir << R"(C:\Users\)" << ::wxGetUserId() << wxFileName::GetPathSeparator()
      << ".wxterminal";
#else
  wxString dir =
      wxFileName::GetHomeDir() + wxFileName::GetPathSeparator() + ".wxterminal";
#endif

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
                  LevelToString(level) + "] " + msg;
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
TerminalLogger::LogEntry::operator<<(const wxRect &rect) {
  if (m_enabled) {
    m_ss << "Rect{" << rect.x << "," << rect.y << "," << rect.width << ","
         << rect.height << "}";
  }
  return *this;
}

TerminalLogger::LogEntry &
TerminalLogger::LogEntry::operator<<(const wxPoint &point) {
  if (m_enabled) {
    m_ss << "Point{" << point.x << "," << point.y << "}";
  }
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

TerminalLogger::LogEntry &
TerminalLogger::LogEntry::operator<<(const std::vector<std::string> &arr) {
  if (m_enabled) {
    m_ss << "[";
    for (size_t i = 0; i < arr.size(); ++i) {
      if (i > 0)
        m_ss << ", ";
      m_ss << arr[i];
    }
    m_ss << "]";
  }
  return *this;
}
