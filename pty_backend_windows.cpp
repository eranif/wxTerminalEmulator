#include "pty_backend_windows.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace terminal {

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
    if (hKernel || hKernelBase)
      return true;

    hKernel = LoadLibraryW(L"kernel32.dll");
    if (!hKernel)
      return false;

    CreatePseudoConsole = reinterpret_cast<CreatePseudoConsoleFn>(
        GetProcAddress(hKernel, "CreatePseudoConsole"));
    ResizePseudoConsole = reinterpret_cast<ResizePseudoConsoleFn>(
        GetProcAddress(hKernel, "ResizePseudoConsole"));
    ClosePseudoConsole = reinterpret_cast<ClosePseudoConsoleFn>(
        GetProcAddress(hKernel, "ClosePseudoConsole"));

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
  char buf[MAX_PATH] = {};
  DWORD n = GetEnvironmentVariableA("COMSPEC", buf, MAX_PATH);
  if (n > 0 && n < MAX_PATH)
    return buf;
  return "C:\\Windows\\System32\\cmd.exe";
}

struct HandleCloser {
  void operator()(HANDLE h) const {
    if (h && h != INVALID_HANDLE_VALUE)
      CloseHandle(h);
  }
};

using unique_handle =
    std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleCloser>;

} // namespace

std::unique_ptr<PtyBackend> PtyBackend::Create() {
  return std::make_unique<WindowsPtyBackend>();
}

WindowsPtyBackend::WindowsPtyBackend() = default;

WindowsPtyBackend::~WindowsPtyBackend() { Stop(); }

bool WindowsPtyBackend::Start(const std::string &command,
                              OutputCallback on_output) {
  Stop();

  m_onOutput = std::move(on_output);

  if (!Api().Load()) {
    if (m_onOutput)
      m_onOutput("[Pseudo console APIs unavailable on this system]\r\n");
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
    if (m_onOutput)
      m_onOutput("[ConPTY functions not found. Windows 10 1809+ required]\r\n");
    return false;
  }

  const std::string shellCommand =
      command.empty() ? GetDefaultShell() : command;

  if (!CreateConPty(shellCommand)) {
    if (m_onOutput)
      m_onOutput("[Failed to create ConPTY backend]\r\n");
    return false;
  }

  m_running = true;
  m_thread = std::thread([this] { ReaderThread(); });
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

void WindowsPtyBackend::Stop() {
  m_running = false;
  m_cv.notify_all();

  if (m_thread.joinable())
    m_thread.join();

  DestroyConPty();
}

bool WindowsPtyBackend::CreateConPty(const std::string &command) {
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  HANDLE inRead = nullptr, inWrite = nullptr;
  HANDLE outRead = nullptr, outWrite = nullptr;

  if (!CreatePipe(&inRead, &inWrite, &sa, 0)) {
    if (m_onOutput)
      m_onOutput("[CreatePipe (input) failed]\r\n");
    return false;
  }
  if (!CreatePipe(&outRead, &outWrite, &sa, 0)) {
    if (m_onOutput)
      m_onOutput("[CreatePipe (output) failed]\r\n");
    CloseHandle(inRead);
    CloseHandle(inWrite);
    return false;
  }

  // Prevent handle inheritance where not needed.
  SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);

  COORD size{};
  size.X = 120;
  size.Y = 30;

  auto createPc = Api().CreatePseudoConsole ? Api().CreatePseudoConsole
                                            : Api().CreatePseudoConsoleDirect;
  HRESULT hr = E_FAIL;
  if (createPc) {
    hr = createPc(size, inRead, outWrite, 0, &m_hPC);
  }

  CloseHandle(inRead);
  CloseHandle(outWrite);

  if (FAILED(hr)) {
    if (m_onOutput) {
      char buf[128];
      snprintf(buf, sizeof(buf),
               "[CreatePseudoConsole failed with HRESULT 0x%08lX]\r\n",
               static_cast<unsigned long>(hr));
      m_onOutput(buf);
    }
    CloseHandle(inWrite);
    CloseHandle(outRead);
    return false;
  }

  STARTUPINFOEXW siex{};
  siex.StartupInfo.cb = sizeof(siex);

  SIZE_T attrListSize = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);

  std::vector<char> attrBuf(attrListSize);
  siex.lpAttributeList =
      reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());

  if (!InitializeProcThreadAttributeList(siex.lpAttributeList, 1, 0,
                                         &attrListSize)) {
    if (m_onOutput) {
      char buf[128];
      snprintf(buf, sizeof(buf),
               "[InitializeProcThreadAttributeList failed: %lu]\r\n",
               GetLastError());
      m_onOutput(buf);
    }
    DestroyConPty();
    CloseHandle(inWrite);
    CloseHandle(outRead);
    return false;
  }

  if (!UpdateProcThreadAttribute(siex.lpAttributeList, 0,
                                 PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, m_hPC,
                                 sizeof(m_hPC), nullptr, nullptr)) {
    if (m_onOutput) {
      char buf[128];
      snprintf(buf, sizeof(buf), "[UpdateProcThreadAttribute failed: %lu]\r\n",
               GetLastError());
      m_onOutput(buf);
    }
    DeleteProcThreadAttributeList(siex.lpAttributeList);
    DestroyConPty();
    CloseHandle(inWrite);
    CloseHandle(outRead);
    return false;
  }

  std::wstring cmdline = Utf8ToWide(command);
  if (cmdline.empty()) {
    cmdline = L"cmd.exe";
  }

  // CreateProcessW may modify the buffer.
  std::vector<wchar_t> mutableCmd(cmdline.begin(), cmdline.end());
  mutableCmd.push_back(L'\0');

  PROCESS_INFORMATION pi{};
  DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;

  BOOL ok = CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                           flags, nullptr, nullptr, &siex.StartupInfo, &pi);

  DeleteProcThreadAttributeList(siex.lpAttributeList);

  if (!ok) {
    if (m_onOutput) {
      char buf[128];
      snprintf(buf, sizeof(buf), "[CreateProcessW failed: %lu]\r\n",
               GetLastError());
      m_onOutput(buf);
    }
    DestroyConPty();
    CloseHandle(inWrite);
    CloseHandle(outRead);
    return false;
  }

  m_hInputWrite = inWrite;
  m_hOutputRead = outRead;
  m_hProcess = pi.hProcess;
  m_hThread = pi.hThread;

  return true;
}

void WindowsPtyBackend::DestroyConPty() {
  if (m_hProcess) {
    TerminateProcess(m_hProcess, 0);
    CloseHandle(m_hProcess);
    m_hProcess = nullptr;
  }

  if (m_hThread) {
    CloseHandle(m_hThread);
    m_hThread = nullptr;
  }

  if (m_hInputWrite) {
    CloseHandle(m_hInputWrite);
    m_hInputWrite = nullptr;
  }

  if (m_hOutputRead) {
    CloseHandle(m_hOutputRead);
    m_hOutputRead = nullptr;
  }

  if (m_hPC) {
    if (Api().ClosePseudoConsole)
      Api().ClosePseudoConsole(m_hPC);
    else if (Api().ClosePseudoConsoleDirect)
      Api().ClosePseudoConsoleDirect(m_hPC);
    m_hPC = nullptr;
  }
}

void WindowsPtyBackend::ReaderThread() {
  char buf[4096];

  while (m_running) {
    // Drain outbound input buffer first.
    std::vector<char> pending;
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_cv.wait_for(lock, std::chrono::milliseconds(1),
                    [this] { return !m_running || !m_writeBuffer.empty(); });

      if (!m_writeBuffer.empty()) {
        pending.swap(m_writeBuffer);
      }
    }

    if (!pending.empty() && m_hInputWrite) {
      DWORD written = 0;
      WriteFile(m_hInputWrite, pending.data(),
                static_cast<DWORD>(pending.size()), &written, nullptr);
    }

    if (m_hOutputRead) {
      DWORD read = 0;
      DWORD avail = 0;

      // Check if there's data available before attempting to read
      if (PeekNamedPipe(m_hOutputRead, nullptr, 0, nullptr, &avail, nullptr) &&
          avail > 0) {
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
      } else {
        // PeekNamedPipe failed - check the error
        DWORD err = GetLastError();
        if (err == ERROR_BROKEN_PIPE || err == ERROR_INVALID_HANDLE) {
          m_running = false;
          break;
        }
      }
    }
  }
}

} // namespace terminal