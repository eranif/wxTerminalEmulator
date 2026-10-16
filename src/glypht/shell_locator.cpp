#include "shell_locator.h"

#include <wx/arrstr.h>
#include <wx/filefn.h>
#include <wx/utils.h>

namespace {
/// Full path of the executable `name` in PATH, or an empty string.
wxString Which(const wxString &name) {
  wxPathList paths;
  paths.AddEnvList("PATH");
  return paths.FindValidPath(name);
}

wxString Quote(const wxString &s) { return s.Contains(' ') ? "\"" + s + "\"" : s; }

#ifdef __WXMSW__
void AddWslDistributions(std::vector<ShellOption> &shells) {
  const wxString wsl = Which("wsl.exe");
  if (wsl.empty()) {
    return;
  }

  // WSL_UTF8=1 makes wsl.exe write UTF-8 instead of UTF-16.
  wxExecuteEnv env;
  wxGetEnvMap(&env.env);
  env.env["WSL_UTF8"] = "1";

  wxArrayString output;
  wxArrayString errors;
  const wxString command = Quote(wsl) + " -l -q";
  if (wxExecute(command, output, errors, wxEXEC_SYNC | wxEXEC_HIDE_CONSOLE,
                &env) != 0) {
    return;
  }

  for (wxString line : output) {
    line.Trim().Trim(false);
    // Skip the header line of old wsl.exe versions and empty lines.
    if (line.empty() ||
        line.Lower().Contains("windows subsystem for linux distributions")) {
      continue;
    }
    // Do not quote the name: wsl.exe does not strip quotes from a distribution
    // name given on a raw command line and then fails with "no distribution
    // with the supplied name". Distribution names have no spaces.
    shells.push_back(
        {"WSL: " + line, Quote(wsl) + " --distribution " + line + " --cd ~"});
  }
}
#endif
} // namespace

std::vector<ShellOption> LocateShells() {
  std::vector<ShellOption> shells;

  struct Candidate {
    const char *title;
    const char *executable;
    const char *args;
  };
  static const Candidate candidates[] = {
#ifdef __WXMSW__
      {"Command Prompt", "cmd.exe", ""},
      {"Windows PowerShell", "powershell.exe", ""},
      {"PowerShell", "pwsh.exe", ""},
      {"Bash", "bash.exe", " --login -i"},
      {"Zsh", "zsh.exe", " --login -i"},
#else
      {"Bash", "bash", " --login -i"},
      {"Zsh", "zsh", " --login -i"},
#endif
  };

  for (const auto &candidate : candidates) {
    const wxString path = Which(candidate.executable);
    if (!path.empty()) {
      shells.push_back({candidate.title, Quote(path) + candidate.args});
    }
  }

#ifdef __WXMSW__
  AddWslDistributions(shells);
#endif
  return shells;
}
