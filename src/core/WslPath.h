#pragma once

#include <wx/string.h>

// Paths of agents that run in a WSL distro. Their working directory and the
// paths in their terminal are Linux paths, which Kennel (a Windows process)
// can only reach through the distro's file share (\\wsl.localhost\<distro>\...)
// or, for the Windows drives, as C:\... . GUI-free.
namespace wsl {

// The distro a login shell command runs in (the value of `--distribution` in
// "wsl.exe --distribution "Debian" --cd ..."), or empty if it is not WSL.
wxString DistroOf(const wxString &loginShell);

// `$HOME` in `distro`. Runs wsl.exe the first time (a fraction of a second, so
// not from a hot path), then it is cached. Empty if that failed.
wxString HomeDir(const wxString &distro);

// The Linux path `text` as written in a terminal, made absolute: "~" and "~/x"
// are under the distro's home, a relative path is under `workingDir` (the
// distro's home if that is empty), and "." / ".." are resolved. Empty if it
// cannot be resolved.
wxString ResolveLinuxPath(const wxString &distro, const wxString &text,
                          const wxString &workingDir);

// The Windows path of the absolute Linux path `linuxPath`: "/mnt/c/x" is
// "C:\x", everything else "\\wsl.localhost\<distro>\...". Pure string work.
wxString ToWindowsPath(const wxString &distro, const wxString &linuxPath);

} // namespace wsl
