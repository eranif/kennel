#pragma once

#include <optional>
#include <string>
#include <vector>
#include <wx/string.h>

// The command lines that Kennel gives to tmux (see
// .agents/planning/tmux-sessions/plan.md). Pure string work: nothing here runs
// a program (TmuxClient does), so it needs no machine and no thread. GUI-free.
//
// Every function that returns a command line gives one for a POSIX shell (sh,
// bash, zsh): a terminal on the machine of the session, or `sh -c` there.
// Words are quoted, so any name, folder or text is safe in them.
namespace tmux {

// Two servers, so that the settings of one kind of session never leak into
// the other (`set-clipboard` is a setting of the whole server): the agents of
// Kennel, and its plain terminals. Both are separate from the tmux of the user
// (`tmux -L kennel ls` shows them).
enum class Server { kAgent, kTerminal };

// The socket name (`-L`) of a server: "kennel" or "kennel-term".
wxString SocketOf(Server server);

struct Version {
  int major{0};
  int minor{0};

  bool IsAtLeast(int atLeastMajor, int atLeastMinor) const {
    return major > atLeastMajor ||
           (major == atLeastMajor && minor >= atLeastMinor);
  }
};

// The oldest tmux Kennel runs sessions in: `allow-passthrough` (3.3) is what
// lets the activity icon of a tab reach Kennel through tmux.
constexpr Version kMinVersion{3, 3};

// The version in what `tmux -V` prints: "tmux 3.4", "tmux 3.5a" (3.5),
// "tmux next-3.5" (3.5), "tmux master" (a build of the development branch: very
// new). Nothing when there is no number in it.
std::optional<Version> ParseVersion(const wxString &output);

// ---------------------------------------------------------------------------
// Settings: tmux commands as the user writes them
// ---------------------------------------------------------------------------

// One tmux command as its words: {"set", "-g", "mouse", "on"}.
using Command = std::vector<wxString>;

// The settings that are set for the sessions of each server, when the user has
// not changed them: one tmux command per line (see ParseCommands()). The
// activity icon needs `allow-passthrough all`; AdaptToVersion() makes it `on`
// (3.3) or drops it (older), so the text never has to say.
wxString DefaultAgentSettings();
wxString DefaultTerminalSettings();

// The commands in `text`: separated by a new line or `;`; the words by white
// space; `'...'` and `"..."` hold a word with spaces (in `"..."`, a backslash
// escapes the next character, as outside quotes); a line that starts with `#`
// is a comment; empty commands are dropped.
std::vector<Command> ParseCommands(const wxString &text);

// `commands` for the tmux of `version`: `allow-passthrough all` becomes `on`
// before 3.4, and any `allow-passthrough` is dropped before 3.3 (tmux refuses
// the option).
std::vector<Command> AdaptToVersion(const std::vector<Command> &commands,
                                    const Version &version);

// ---------------------------------------------------------------------------
// The command lines
// ---------------------------------------------------------------------------

// The tmux session name for a Kennel session: "kennel-<group>-<session>", both
// names as the user gave them but normalised (see below): "kennel-Default-fix
// login bug" is "kennel-Default-fix-login-bug". Made once per session and
// saved with it, so a rename of the session does not lose its tmux session.
//
// Normalised: white space and control characters, `.` and `:` (tmux does not
// allow the last two in a name) become `-`, and a run of `-` is one. Two
// sessions can still end up with the same name (the names "a b" and "a-b"):
// the caller checks that (MainNotebook::LaunchSession()).
wxString SessionNameFor(const wxString &group, const wxString &session);

struct LaunchSpec {
  Server server{Server::kAgent};
  // The name of the tmux session (see SessionNameFor()).
  wxString sessionName;
  // What runs in the session, as one `sh` command line: "claude --continue ||
  // claude". It is passed to `sh -c`, so it is POSIX whatever the shell of the
  // user is. Empty: the shell of the user.
  wxString command;
  // The settings to set on this line, already adapted (AdaptToVersion()). They
  // are set on the line (and not through tmux-shim) because the shim may not be
  // on the machine, and they must hold in both cases.
  std::vector<Command> settings;
};

// Attaches to the tmux session, or creates it and runs the command in it when
// it does not exist (`new-session -A`, so it is one step with no check before
// it that could race). It has no `-d`: a second client shares the session.
//
//   env -u TMUX tmux -L kennel -f /dev/null new-session -A -s kennel-1a2b3c4d \
//     sh -c 'claude --continue || claude' \
//     \; set-option -g status off \; set-option -g set-titles on \; ...
//
// `env -u TMUX` lets it run in a shell that is inside the tmux of the user. The
// settings that must hold before the first line is drawn (no status line, the
// title of the window, the size that follows the Kennel terminal) are always
// on it; `spec.settings` follow.
wxString AttachOrCreate(const LaunchSpec &spec);

// What goes in front of the program in a command, to run it through tmux-shim
// (see TmuxShim.h): `"$HOME/.kennel/bin/tmux-shim" --`. `shimPath` is in the
// notation of a shell and is put in double quotes (so `$HOME` is expanded where
// the command runs).
wxString ShimPrefix(const wxString &shimPath);

// What a plain terminal runs in tmux: the shell of the machine (`$SHELL`, else
// `sh`) as a login shell, as tmux starts its own, through tmux-shim when that
// is installed on the machine (checked there, when the command runs). The shim
// needs a program to run, which is why the shell is named and not left to tmux:
// it is what carries the folder and the title of the shell through tmux. It is
// `$SHELL` of the machine, not the shell that Kennel picked for the terminal:
// that one is a command that logs in (`wsl.exe -d ...`, the shell of this
// machine), and tmux itself started the shell of the machine (its
// `default-shell`) before this change as well.
wxString TerminalCommand(const wxString &shimPath);

// The answers about the sessions of a server. Each exits with 0 when the thing
// is true or done. `name` is matched exactly (not as a prefix).
wxString HasSession(Server server, const wxString &name);
wxString KillSession(Server server, const wxString &name);
// One line for each session: "<name>|<created>|<attached>" (ParseSessions()).
wxString ListSessions(Server server);
// Prints the version (ParseVersion()).
wxString VersionProbe();

struct SessionInfo {
  wxString name;
  long long created{0}; // Seconds since 1970
  int attached{0};      // How many clients are attached
};

// The output of ListSessions(): one session for each line that fits.
std::vector<SessionInfo> ParseSessions(const wxString &output);

} // namespace tmux
