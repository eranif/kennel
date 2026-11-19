#pragma once

#include <wx/string.h>

#include "core/Config.h"
#include "core/TmuxCommands.h"

#include <optional>
#include <vector>

// The CLI tools Kennel supports. Anything else needs code changes here: the
// resume and one-shot arguments are fixed per tool, not set by the user.
enum class ClientKind { Unknown, Claude, Codex, Kiro, OpenCode };

// The tool behind `executable`: the file name decides (a path, a ".exe" or a
// ".cmd" is fine).
ClientKind ClientKindOf(const wxString &executable);
inline bool IsSupportedClient(const wxString &executable) {
  return ClientKindOf(executable) != ClientKind::Unknown;
}

// Sets agent.resumeArg and agent.nonInteractiveArg from the tool. Both are
// empty for an unsupported tool.
void ApplyClientDefaults(AgentDef &agent);

// The shipped icon (a file name in the assets folder) for an agent that has
// none: by tool, and by where it runs (remote, WSL or local). Needs the
// executable, remoteHost and loginShell of `agent`.
wxString DefaultIconFor(const AgentDef &agent);

// `cd "<dir>"` for a POSIX shell. In double quotes. Only a leading `$HOME` (or
// a leading `~`, which is written as `$HOME`: a tilde is not expanded inside
// quotes) is expanded by the shell; every other `$`, and a backtick, is
// escaped, so that a folder name of a host (not trusted) never runs a command
// or changes the folder.
wxString ChangeDirCommand(const wxString &dir);

// The command that logs in to `host` over ssh in a terminal (a shell on this
// machine): `ssh -o ServerAliveInterval=10 [user@]host`. `user` empty: the ssh
// default.
//
// Not on Windows: the shell of this machine then resets the terminal when ssh
// ends (`; printf ...`, a soft reset, DECSTR, and bracketed paste off). A
// connection that drops (or `~.`) gives the program on the host (tmux) no
// chance to undo its modes: the mouse would stay tracked (its reports are
// typed at the prompt), the cursor hidden, the alternate screen shown. The
// soft reset keeps the text of the screen and the scrollback. The shells of
// Windows (PowerShell, cmd) have no `printf`.
wxString SshLoginCommand(const wxString &host, const wxString &user);

// Builds the list of commands to send to the terminal to launch a session.
// When resume is true and agent.resumeArg is non-empty, the resume arg is
// appended before agent.extraArgs. The command is then
// "<resume cmd> || <plain cmd>", so a fresh session starts when there is
// nothing to resume. This also starts a new session when the resumed one ends
// with a non-zero exit code.
// A non-empty `initialPrompt` is appended last, as a quoted argument: the agent
// starts interactively and submits it as its first message
// (`kiro-cli chat "<prompt>"`, `claude "<prompt>"`). For kiro-cli, `chat` is
// added before the other arguments if no argument has it. The prompt is
// escaped for a POSIX shell (also over ssh and in WSL). A Windows shell (cmd,
// PowerShell) does not understand that escaping, so keep the prompt to plain
// text there.
// With `tmuxSpec` (its `command` is made here) the program runs in a tmux
// session (attached when it exists, created with the program otherwise; see
// tmux::AttachOrCreate()), through tmux-shim when it is installed on the
// machine.
std::vector<wxString> BuildCommandLine(
    const AgentDef &agent, const wxString &workingDir, bool resume,
    const wxString &initialPrompt = {},
    const std::optional<tmux::LaunchSpec> &tmuxSpec = std::nullopt);

// Builds the list of commands to send to the terminal to run a single
// "Prompt" job non-interactively: like BuildCommandLine, but appends
// agent.nonInteractiveArg (instead of resumeArg) and the prompt text as the
// final quoted argument.
std::vector<wxString> BuildJobCommandLine(const AgentDef &agent,
                                          const wxString &workingDir,
                                          const wxString &prompt);
