// clang-format off
#include <wx/app.h>
// clang-format on

#include "pty_backend_windows.h"
#include "terminal_event.h"
#include "terminal_logger.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <psapi.h>
#include <string>
#include <tlhelp32.h>
#include <unordered_set>
#include <vector>

namespace terminal {

// ConPTY creation flags (from Windows SDK)
#ifndef PSEUDOCONSOLE_INHERIT_CURSOR
#define PSEUDOCONSOLE_INHERIT_CURSOR 0x1
#endif

namespace {

using CreatePseudoConsoleFn = HRESULT(WINAPI *)(COORD, HANDLE, HANDLE, DWORD,
                                                HPCON *);
using ResizePseudoConsoleFn = HRESULT(WINAPI *)(HPCON, COORD);
using ClosePseudoConsoleFn = void(WINAPI *)(HPCON);

using CreatePseudoConsoleDirectFn = HRESULT(WINAPI *)(COORD, HANDLE, HANDLE,
                                                      DWORD, HPCON *);
using ResizePseudoConsoleDirectFn = HRESULT(WINAPI *)(HPCON, COORD);
using ClosePseudoConsoleDirectFn = void(WINAPI *)(HPCON);

struct ConPtyApi {
  HMODULE hKernel{nullptr};
  HMODULE hKernelBase{nullptr};
  CreatePseudoConsoleFn CreatePseudoConsole{nullptr};
  ResizePseudoConsoleFn ResizePseudoConsole{nullptr};
  ClosePseudoConsoleFn ClosePseudoConsole{nullptr};
  CreatePseudoConsoleDirectFn CreatePseudoConsoleDirect{nullptr};
  ResizePseudoConsoleDirectFn ResizePseudoConsoleDirect{nullptr};
  ClosePseudoConsoleDirectFn ClosePseudoConsoleDirect{nullptr};

  bool Load() {
    static std::mutex loadMutex;
    std::lock_guard<std::mutex> lock(loadMutex);

    // Check again after acquiring lock
    if (hKernel && hKernelBase)
      return true;

    if (!hKernel && !(hKernel = LoadLibraryW(L"kernel32.dll")))
      return false;

    CreatePseudoConsole = reinterpret_cast<CreatePseudoConsoleFn>(
        GetProcAddress(hKernel, "CreatePseudoConsole"));
    ResizePseudoConsole = reinterpret_cast<ResizePseudoConsoleFn>(
        GetProcAddress(hKernel, "ResizePseudoConsole"));
    ClosePseudoConsole = reinterpret_cast<ClosePseudoConsoleFn>(
        GetProcAddress(hKernel, "ClosePseudoConsole"));

    // Try to load kernelbase.dll (optional)
    if (!hKernelBase)
      hKernelBase = LoadLibraryW(L"kernelbase.dll");
    if (hKernelBase) {
      CreatePseudoConsoleDirect = reinterpret_cast<CreatePseudoConsoleDirectFn>(
          GetProcAddress(hKernelBase, "CreatePseudoConsole"));
      ResizePseudoConsoleDirect = reinterpret_cast<ResizePseudoConsoleDirectFn>(
          GetProcAddress(hKernelBase, "ResizePseudoConsole"));
      ClosePseudoConsoleDirect = reinterpret_cast<ClosePseudoConsoleDirectFn>(
          GetProcAddress(hKernelBase, "ClosePseudoConsole"));
    }

    // Some Windows SDK/runtime combinations don't expose the functions with the
    // same availability check via GetProcAddress, so treat load success as
    // availability and let CreateConPty report the actual HRESULT.
    return true;
  }

  ~ConPtyApi() {
    if (hKernel) {
      FreeLibrary(hKernel);
      hKernel = nullptr;
    }
    if (hKernelBase) {
      FreeLibrary(hKernelBase);
      hKernelBase = nullptr;
    }
  }
};

ConPtyApi &Api() {
  static ConPtyApi api;
  return api;
}

std::wstring Utf8ToWide(const std::string &utf8) {
  if (utf8.empty())
    return {};
  const int len = MultiByteToWideChar(
      CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
  if (len <= 0)
    return {};

  std::wstring wide(static_cast<size_t>(len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()),
                      wide.data(), len);
  return wide;
}

std::string GetDefaultShell() {
  // Favour PowerShell
  static const wxString CMD_SHELL = R"(C:\Windows\System32\cmd.exe)";

  char buf[MAX_PATH] = {};
  WORD n = GetEnvironmentVariableA("COMSPEC", buf, MAX_PATH);
  if (n > 0 && n < MAX_PATH)
    return buf;

  return CMD_SHELL.ToStdString(wxConvUTF8);
}

std::vector<wchar_t>
BuildEnvironmentBlock(const std::optional<PtyBackend::EnvironmentList> &env) {
  if (!env.has_value())
    return {};

  if (env->empty())
    return {L'\0', L'\0'};

  std::vector<std::wstring> envWide;
  envWide.reserve(env->size());

  std::vector<wchar_t> block;
  for (const auto &entry : *env) {
    envWide.push_back(Utf8ToWide(entry));
    const auto &wide = envWide.back();
    block.insert(block.end(), wide.begin(), wide.end());
    block.push_back(L'\0');
  }
  block.push_back(L'\0');
  return block;
}

struct HandleCloser {
  void operator()(HANDLE h) const {
    if (h && h != INVALID_HANDLE_VALUE)
      CloseHandle(h);
  }
};

using unique_handle =
    std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleCloser>;

std::vector<WindowsPtyBackend::ChildProcessInfo>
CollectDirectChildren(DWORD parentPid) {
  std::vector<WindowsPtyBackend::ChildProcessInfo> children;

  unique_handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
  if (!snapshot || snapshot.get() == INVALID_HANDLE_VALUE)
    return children;

  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);

  if (!Process32FirstW(snapshot.get(), &entry))
    return children;

  do {
    if (entry.th32ParentProcessID == parentPid) {
      children.push_back({entry.th32ProcessID, entry.szExeFile});
    }
  } while (Process32NextW(snapshot.get(), &entry));

  return children;
}

} // namespace

std::unique_ptr<PtyBackend> PtyBackend::Create(wxEvtHandler *eventHandler) {
  return std::make_unique<WindowsPtyBackend>(eventHandler);
}

WindowsPtyBackend::WindowsPtyBackend(wxEvtHandler *eventHandler)
    : PtyBackend{eventHandler} {}

WindowsPtyBackend::~WindowsPtyBackend() { Stop(); }

bool WindowsPtyBackend::Start(const std::string &command,
                              const std::optional<EnvironmentList> &environment,
                              OutputCallback on_output) {
  Stop();

  m_onOutput = std::move(on_output);

  if (!Api().Load()) {
    TLOG_WARN() << "[Pseudo console APIs unavailable on this system]"
                << std::endl;
    if (m_onOutput) {
      m_onOutput("[Pseudo console APIs unavailable on this system]\r\n");
    }
    return false;
  }

  // Verify the API functions are actually available
  auto createPc = Api().CreatePseudoConsole ? Api().CreatePseudoConsole
                                            : Api().CreatePseudoConsoleDirect;
  auto resizePc = Api().ResizePseudoConsole ? Api().ResizePseudoConsole
                                            : Api().ResizePseudoConsoleDirect;
  auto closePc = Api().ClosePseudoConsole ? Api().ClosePseudoConsole
                                          : Api().ClosePseudoConsoleDirect;

  if (!createPc || !resizePc || !closePc) {
    if (m_onOutput) {
      TLOG_WARN() << "[ConPTY functions not found. Windows 10 1809+ required]"
                  << std::endl;
      m_onOutput("[ConPTY functions not found. Windows 10 1809+ required]\r\n");
    }
    return false;
  }

  const std::string shellCommand =
      command.empty() ? GetDefaultShell() : command;

  if (!CreateConPty(shellCommand, environment)) {
    if (m_onOutput) {
      TLOG_WARN() << "[Failed to create ConPTY backend]" << std::endl;
      m_onOutput("[Failed to create ConPTY backend]\r\n");
    }
    return false;
  }

  m_running = true;
  m_readerThread = std::thread([this] { ReaderThread(); });
  m_writerThread = std::thread([this] { WriterThread(); });
  return true;
}

void WindowsPtyBackend::Write(const std::string &data) {
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_writeBuffer.insert(m_writeBuffer.end(), data.begin(), data.end());
  }
  m_cv.notify_one();
}

void WindowsPtyBackend::Resize(int cols, int rows) {
  if (!m_hPC)
    return;

  auto resizePc = Api().ResizePseudoConsole ? Api().ResizePseudoConsole
                                            : Api().ResizePseudoConsoleDirect;
  if (!resizePc)
    return;

  COORD size{};
  size.X = static_cast<SHORT>(cols);
  size.Y = static_cast<SHORT>(rows);

  resizePc(m_hPC, size);
}

void WindowsPtyBackend::SendBreak() { Write("\x03"); }

void WindowsPtyBackend::Stop() {
  m_running = false;
  m_cv.notify_all();

  InterruptIo();

  if (m_readerThread.joinable())
    m_readerThread.join();
  if (m_writerThread.joinable())
    m_writerThread.join();

  DestroyConPty();
}

bool WindowsPtyBackend::CreateConPty(
    const std::string &command,
    const std::optional<EnvironmentList> &environment) {
  if (!Api().Load()) {
    TLOG_WARN() << "[ConPTY APIs unavailable on this system]" << std::endl;
    if (m_onOutput) {
      m_onOutput("[ConPTY APIs unavailable on this system]\r\n");
    }
    return false;
  }

  // Verify the API functions are actually available
  auto createPc = Api().CreatePseudoConsole ? Api().CreatePseudoConsole
                                            : Api().CreatePseudoConsoleDirect;
  auto resizePc = Api().ResizePseudoConsole ? Api().ResizePseudoConsole
                                            : Api().ResizePseudoConsoleDirect;
  auto closePc = Api().ClosePseudoConsole ? Api().ClosePseudoConsole
                                          : Api().ClosePseudoConsoleDirect;

  if (!createPc || !resizePc || !closePc) {
    TLOG_WARN() << "[ConPTY functions not found. Windows 10 1809+ required]"
                << std::endl;
    if (m_onOutput) {
      m_onOutput("[ConPTY functions not found. Windows 10 1809+ required]\r\n");
    }
    return false;
  }

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  // Create pipes for ConPTY communication
  // Child-side handles (inRead, outWrite) are inherited by the child process
  // Parent-side handles (inWrite, outRead) are used by our reader/writer
  // threads

  HANDLE inRead = nullptr, inWrite = nullptr;
  HANDLE outRead = nullptr, outWrite = nullptr;

  // Use RAII wrappers for automatic cleanup on failure
  auto cleanup = [&]() {
    if (inRead && inRead != INVALID_HANDLE_VALUE)
      CloseHandle(inRead);
    if (inWrite && inWrite != INVALID_HANDLE_VALUE)
      CloseHandle(inWrite);
    if (outRead && outRead != INVALID_HANDLE_VALUE)
      CloseHandle(outRead);
    if (outWrite && outWrite != INVALID_HANDLE_VALUE)
      CloseHandle(outWrite);
  };

  if (!CreatePipe(&inRead, &inWrite, &sa, 0)) {
    DWORD err = GetLastError();
    TLOG_ERROR() << "[CreatePipe (input) failed: " << err << "]" << std::endl;
    if (m_onOutput) {
      char buf[128];
      snprintf(buf, sizeof(buf), "[CreatePipe (input) failed: %lu]\r\n", err);
      m_onOutput(buf);
    }
    return false;
  }

  // Validate handles
  if (inRead == INVALID_HANDLE_VALUE || inWrite == INVALID_HANDLE_VALUE) {
    TLOG_ERROR() << "[CreatePipe returned INVALID_HANDLE_VALUE]" << std::endl;
    if (m_onOutput)
      m_onOutput("[CreatePipe returned INVALID_HANDLE_VALUE]\r\n");
    cleanup();
    return false;
  }

  if (!CreatePipe(&outRead, &outWrite, &sa, 0)) {
    DWORD err = GetLastError();
    TLOG_ERROR() << "[CreatePipe (output) failed: " << err << "]" << std::endl;
    if (m_onOutput) {
      char buf[128];
      snprintf(buf, sizeof(buf), "[CreatePipe (output) failed: %lu]\r\n", err);
      m_onOutput(buf);
    }
    cleanup();
    return false;
  }

  // Validate handles
  if (outRead == INVALID_HANDLE_VALUE || outWrite == INVALID_HANDLE_VALUE) {
    TLOG_ERROR() << "[CreatePipe returned INVALID_HANDLE_VALUE]" << std::endl;
    if (m_onOutput)
      m_onOutput("[CreatePipe returned INVALID_HANDLE_VALUE]\r\n");
    cleanup();
    return false;
  }

  // Prevent handle inheritance for parent-side handles
  // Only child-side handles (inRead, outWrite) should be inherited
  SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

  COORD size{};
  size.X = 120;
  size.Y = 30;

  HRESULT hr = E_FAIL;
  HPCON hPC = nullptr;

  // Use PSEUDOCONSOLE_INHERIT_CURSOR to improve compatibility
  DWORD dwFlags = PSEUDOCONSOLE_INHERIT_CURSOR;
  hr = createPc(size, inRead, outWrite, dwFlags, &hPC);

  // Close child-side handles in parent process immediately after ConPTY
  // creation ConPTY has duplicated these handles internally
  CloseHandle(inRead);
  CloseHandle(outWrite);
  inRead = nullptr;
  outWrite = nullptr;

  if (FAILED(hr)) {
    TLOG_ERROR() << "[CreatePseudoConsole failed with HRESULT 0x" << std::hex
                 << hr << std::dec << "]" << std::endl;
    if (m_onOutput) {
      char buf[128];
      snprintf(buf, sizeof(buf),
               "[CreatePseudoConsole failed with HRESULT 0x%08lX]\r\n",
               static_cast<unsigned long>(hr));
      m_onOutput(buf);
    }
    cleanup();
    return false;
  }

  // Initialize extended startup info for process creation
  STARTUPINFOEXW siex{};
  siex.StartupInfo.cb = sizeof(siex);

  SIZE_T attrListSize = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);

  if (attrListSize == 0) {
    DWORD err = GetLastError();
    TLOG_ERROR() << "[InitializeProcThreadAttributeList size query failed: "
                 << err << "]" << std::endl;
    if (m_onOutput) {
      char buf[128];
      snprintf(buf, sizeof(buf),
               "[InitializeProcThreadAttributeList size query failed: %lu]\r\n",
               err);
      m_onOutput(buf);
    }
    closePc(hPC);
    cleanup();
    return false;
  }

  std::vector<char> attrBuf(attrListSize);
  siex.lpAttributeList =
      reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());

  if (!InitializeProcThreadAttributeList(siex.lpAttributeList, 1, 0,
                                         &attrListSize)) {
    DWORD err = GetLastError();
    TLOG_ERROR() << "[InitializeProcThreadAttributeList failed: " << err << "]"
                 << std::endl;
    if (m_onOutput) {
      char buf[128];
      snprintf(buf, sizeof(buf),
               "[InitializeProcThreadAttributeList failed: %lu]\r\n", err);
      m_onOutput(buf);
    }
    closePc(hPC);
    cleanup();
    return false;
  }

  // Associate the ConPTY with the process
  if (!UpdateProcThreadAttribute(siex.lpAttributeList, 0,
                                 PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hPC,
                                 sizeof(hPC), nullptr, nullptr)) {
    DWORD err = GetLastError();
    TLOG_ERROR() << "[UpdateProcThreadAttribute failed: " << err << "]"
                 << std::endl;
    if (m_onOutput) {
      char buf[128];
      snprintf(buf, sizeof(buf), "[UpdateProcThreadAttribute failed: %lu]\r\n",
               err);
      m_onOutput(buf);
    }
    DeleteProcThreadAttributeList(siex.lpAttributeList);
    closePc(hPC);
    cleanup();
    return false;
  }

  // Prepare command line
  std::wstring cmdline = Utf8ToWide(command);
  if (cmdline.empty()) {
    cmdline = LR"(C:\Windows\System32\cmd.exe)";
  }

  // CreateProcessW requires a mutable buffer for the command line
  std::vector<wchar_t> mutableCmd(cmdline.begin(), cmdline.end());
  mutableCmd.push_back(L'\0');

  PROCESS_INFORMATION pi{};
  ZeroMemory(&pi, sizeof(pi));

  std::vector<wchar_t> envBlock = BuildEnvironmentBlock(environment);

  // Use CREATE_UNICODE_ENVIRONMENT flag when passing Unicode environment
  DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;

  // IMPORTANT: Pass nullptr as lpApplicationName and mutableCmd.data() as
  // lpCommandLine This allows proper command line parsing and shell
  // interpretation
  BOOL ok = CreateProcessW(
      nullptr,           // lpApplicationName - nullptr to use command line
      mutableCmd.data(), // lpCommandLine - mutable buffer with full command
      nullptr,           // lpProcessAttributes
      nullptr,           // lpThreadAttributes
      FALSE,             // bInheritHandles - FALSE because we use
                         // PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
      flags,             // dwCreationFlags
      envBlock.empty() ? nullptr : envBlock.data(),
      nullptr, // lpCurrentDirectory - nullptr to inherit parent directory
      &siex.StartupInfo, // lpStartupInfo
      &pi);              // lpProcessInformation

  DeleteProcThreadAttributeList(siex.lpAttributeList);

  if (!ok) {
    DWORD err = GetLastError();
    TLOG_ERROR() << "[CreateProcessW failed: " << err
                 << "] Command: " << command << std::endl;
    if (m_onOutput) {
      char buf[128];
      snprintf(buf, sizeof(buf), "[CreateProcessW failed: %lu]\r\n", err);
      m_onOutput(buf);
    }
    closePc(hPC);
    cleanup();
    return false;
  }

  // Success! Store handles in member variables
  // These are now owned by this instance
  m_hInputWrite = inWrite;
  m_hOutputRead = outRead;
  m_hProcess = pi.hProcess;
  m_processPid = pi.dwProcessId;
  m_hThread = pi.hThread;
  m_hPC = hPC;

  TLOG_DEBUG() << "[ConPTY created successfully] PID: " << pi.dwProcessId
               << " Command: " << command << std::endl;

  return true;
}

void WindowsPtyBackend::DestroyConPty() {
  // Gracefully terminate the process first
  if (m_hProcess) {
    // Process didn't exit gracefully, force termination
    TLOG_WARN() << "[Forcefully terminating process]" << std::endl;
    TerminateProcess(m_hProcess, 1);
    WaitForSingleObject(m_hProcess, 1000);

    CloseHandle(m_hProcess);
    m_hProcess = nullptr;
    m_processPid = -1;
  }

  if (m_hThread) {
    CloseHandle(m_hThread);
    m_hThread = nullptr;
  }

  // Close pipe handles
  // These should be closed BEFORE closing the ConPTY to avoid potential
  // deadlocks
  if (m_hInputWrite) {
    // Cancel any pending I/O first
    CancelIoEx(m_hInputWrite, nullptr);
    CloseHandle(m_hInputWrite);
    m_hInputWrite = nullptr;
  }

  if (m_hOutputRead) {
    // Cancel any pending I/O first
    CancelIoEx(m_hOutputRead, nullptr);
    CloseHandle(m_hOutputRead);
    m_hOutputRead = nullptr;
  }

  // Close the ConPTY last
  // This ensures all handles associated with it are closed first
  if (m_hPC) {
    auto closePc = Api().ClosePseudoConsole ? Api().ClosePseudoConsole
                                            : Api().ClosePseudoConsoleDirect;
    if (closePc) {
      TLOG_DEBUG() << "[Closing ConPTY handle]" << std::endl;
      closePc(m_hPC);
    } else {
      TLOG_WARN() << "[ClosePseudoConsole function not available, potential "
                     "resource leak]"
                  << std::endl;
    }
    m_hPC = nullptr;
  }

  TLOG_DEBUG() << "[ConPTY destroyed successfully]" << std::endl;
}

void WindowsPtyBackend::WriterThread() {
  while (m_running) {
    std::vector<char> pending;
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_cv.wait(lock, [this] { return !m_running || !m_writeBuffer.empty(); });

      if (!m_running)
        break;

      if (!m_writeBuffer.empty()) {
        pending.swap(m_writeBuffer);
      }
    }

    if (!pending.empty() && m_hInputWrite) {
      DWORD written = 0;
      if (!WriteFile(m_hInputWrite, pending.data(),
                     static_cast<DWORD>(pending.size()), &written, nullptr)) {
        DWORD err = GetLastError();
        if (err == ERROR_BROKEN_PIPE || err == ERROR_INVALID_HANDLE) {
          m_running = false;
          break;
        }
      }
    }
  }
  TLOG_DEBUG() << "WriterThread is going down" << std::endl;
}

void WindowsPtyBackend::ReaderThread() {
  char buf[4096];

  while (m_running) {
    // Check if process is still alive
    if (m_hProcess) {
      DWORD exitCode = 0;
      if (GetExitCodeProcess(m_hProcess, &exitCode) &&
          exitCode != STILL_ACTIVE) {
        TLOG_WARN() << "Process has exited with code: " << exitCode << ". "
                    << std::hex << exitCode << std::endl;
        m_running = false;
        wxTerminalEvent terminate_event{wxEVT_TERMINAL_TERMINATED};
        terminate_event.SetEventObject(m_eventHandler);
        m_eventHandler->AddPendingEvent(terminate_event);
        break;
      }
    }

    if (m_hOutputRead) {
      DWORD read = 0;
      DWORD avail = 0;

      // Check if there's data available before attempting to read
      if (PeekNamedPipe(m_hOutputRead, nullptr, 0, nullptr, &avail, nullptr)) {
        if (avail > 0) {
          // Limit read to buffer size
          DWORD toRead = (avail < sizeof(buf)) ? avail : sizeof(buf);
          BOOL ok = ReadFile(m_hOutputRead, buf, toRead, &read, nullptr);
          if (ok && read > 0) {
            if (m_onOutput) {
              m_onOutput(std::string(buf, buf + read));
            }
          } else {
            DWORD err = GetLastError();
            if (err == ERROR_BROKEN_PIPE || err == ERROR_INVALID_HANDLE) {
              m_running = false;
              break;
            }
          }
        }
      } else {
        DWORD err = GetLastError();
        if (err == ERROR_BROKEN_PIPE || err == ERROR_INVALID_HANDLE) {
          m_running = false;
          TLOG_WARN() << "Pipe broken!" << std::endl;
          break;
        }
      }
    }

    // Small sleep to avoid tight loop when no data is available
    if (!m_running) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  TLOG_DEBUG() << "ReaderThread is going down" << std::endl;
}

void WindowsPtyBackend::InterruptIo() {
  if (m_hInputWrite)
    CancelIoEx(m_hInputWrite, nullptr);

  if (m_hOutputRead)
    CancelIoEx(m_hOutputRead, nullptr);
}

wxArrayString terminal::WindowsPtyBackend::GetDirectChildren() const {
  if (m_processPid == -1) {
    return {};
  }

  auto children = CollectDirectChildren(m_processPid);
  if (children.empty()) {
    return {};
  }
  wxArrayString result;
  result.reserve(children.size());
  for (const auto &child : children) {
    result.push_back(wxString{child.imageName});
  }
  return result;
}
} // namespace terminal
