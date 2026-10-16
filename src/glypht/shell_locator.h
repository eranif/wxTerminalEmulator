#pragma once

#include <vector>
#include <wx/string.h>

/// A shell the user can pick for a new tab.
struct ShellOption {
  /// Text shown in the list, e.g. "WSL: Ubuntu".
  wxString title;
  /// Command line that starts the shell.
  wxString command;
};

/// Look for the shells installed on this computer: cmd, PowerShell, bash, zsh
/// and, on Windows, every WSL distribution. Runs `wsl -l -q`, so it can take a
/// moment. The result is not cached.
std::vector<ShellOption> LocateShells();
