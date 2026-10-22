# Kennel 2.0.0 Release Notes

*Covers changes since tag `1.4.0`.*

Kennel 2.0.0 makes the terminal page a tool you can work with: every session has a toolbar
(Restart, Review Buddy, Copy, Paste, Find) instead of a right-click menu, a header that shows
what the session is, a search bar, and a thin progress bar. Review Buddy reports its progress
right in that header. You can now choose a dark or a light theme for the whole application,
file links from the terminal follow `cd`, and Windows gets a newer ConPTY and a newer
wxWidgets.

## Major Improvements

### A toolbar and a header for every terminal

- **Terminal toolbar** — a toolbar above every session has **Restart**, **Review Buddy**,
  **Copy**, **Paste** and **Find**. The terminal has no right-click menu any more: the
  actions are in the toolbar. (The old menu's **Clear buffer** entry is gone with it.)
  - **Copy** copies the selected text. When nothing is selected, it copies the text the
    program in the terminal copied itself (for example tmux, or a TUI such as Claude Code),
    all of it and not only the first line. It is disabled when there is nothing to copy.
  - **Paste** and **Find** work on the terminal that has the focus (the agent's or the
    reviewer's).
  - **Review Buddy** is a drop-down. With no review open, it lists the agents on the same host
    to review with ("Review with ..."), or says why a review is not possible (no working
    directory, not a git repository). With a review open, it shows the state and offers
    **Send the Request Again**, **Stop the Review**, **Open the Latest Review** and
    **Close Review Buddy**. It is disabled for plain terminals and scheduled jobs.
- **Header above the terminal** — the agent's terminal shows
  `Agent Name | Title | Working Directory` in a strip above it. It follows the terminal's
  title and the folder the shell is in now (before the shell reports one, the folder the
  session started in). A plain terminal shows `Terminal`. On a dark theme the text is yellow
  on a strip darker than the terminal.
- **Review Buddy in the header** — while a review is open, the header shows only the state of
  the review: a braille spinner (⠋ ⠙ ⠹ ⠸ ⠼ ⠴ ⠦ ⠧) while somebody is working, for example
  `⠹ Waiting for review (round 1/5)` or `⠼ Addressing comments (round 2/5)`. The end of the
  review has a mark in front of the status: 👍 the review is clean, 👎 it stalled or failed,
  🔴 the round limit was reached with findings still open. The header goes back to
  `Agent | Title | Folder` when you close the Review Buddy.
- **Progress bar** — a thin bar above the terminal shows the progress a program reports with
  the OSC 9;4 sequence (the ConEmu / Windows Terminal progress bar) or with OSC 7501 (the
  program status protocol): a percent bar, or a moving bar when there is no percent. It is
  hidden when there is no progress, and it gives the terminal only a few pixels. Try it with
  `printf '\e]9;4;1;40\a'` (and `\e]9;4;0;0\a` to hide it).
- **Toolbar icons for every theme** — new blue Copy, Paste, Find, Save, Close and Reload
  icons, and disabled toolbar buttons that fit the theme: before, the disabled image was
  shiny and white on a dark theme.

### A theme for the whole application

- **System, Dark or Light** — a new **Theme** choice in **Settings** sets the look of the
  application. It is saved in `.persist.json` (`appTheme`) and applied when Kennel starts and
  when you confirm the settings. The default is **Dark**, as before. It is separate from the
  terminal color theme.
- **Toolbars follow the theme** — a light theme gets a plain toolbar background and a dark
  theme the default one. A change in Settings updates the left toolbar and the toolbars of
  the open sessions at once.
- **Stripes on a light theme** — the alternating row colour of the session tree and the flat
  list was a lightened window colour, which does nothing on white. A light background now gets
  darker rows, so the stripes show.
- **Terminal themes fit light backgrounds** — many theme files have no cursor colour, and the
  default is white: the block cursor could not be seen on a light theme. Now:
  - a theme without a cursor colour uses its foreground colour;
  - the selection (`[colors.selection]`) and search match (`[colors.search.matches]`) colors
    of a theme are used, and when a theme sets only one color of a pair (for example
    `CellForeground`), a readable color is picked for the other one;
  - the link color is the theme's own blue (the bright one on a dark background);
  - a color value with a comment after it (`'#303446' # base`) is no longer skipped.

### Search in files and in terminals

- **A search bar instead of a dialog** — Ctrl+F (Cmd+F on macOS), or **Find** in the Search
  menu or in the terminal toolbar, shows a search bar at the bottom of the page, below the
  editor or the terminals. It searches as you type. Enter or the down arrow finds the next
  match, Shift+Enter or the up arrow the previous one, and Escape closes it. The selected
  text is the text to look for.
- **Files** — the search bar also has **Full word** and **Case sensitive**, and a second
  line with **Replace with**, **Replace** and **Replace all** (one undo step). The Find and
  Replace dialog and the Replace menu entry are gone; Tab goes from the find text to the
  replace text, and back.
- **Terminals** — the bar has the search text and the arrows. Closing it clears the
  highlights. This also fixes Find on macOS, where Enter in the old dialog did nothing.
- **F3 and Shift+F3** — find the next and the previous match, whatever the direction is.
  A terminal keeps F3 for the program in it (such as `mc` or `htop`) until you have a
  search. Ctrl+F no longer reaches the program in the terminal on Windows and Linux, so
  readline's "forward one character" does not work there.

### Files opened from the terminal

- **Relative paths follow `cd`** — Kennel asks the shell where it is (OSC 7, and OSC 9;9 for
  cmd.exe). A relative path you click is looked up in that folder first, then in the folder the
  session started in, and in the home folder only when there is no other. This works in plain
  terminals, in WSL and over SSH. The shell integration is added by the terminal for bash
  (also over `ssh`) and cmd.exe, and an existing `PROMPT_COMMAND` is kept.
- **A notice when the file is not found** — the status bar says `File not found: ...`
  instead of doing nothing.
- **Windows with a Linux shell** — a Linux folder of an `ssh` shell is not mixed with a
  Windows path any more. Kennel says that the file may be on another machine. msys2 and Git
  Bash paths (`/c/Users/...`) are mapped to drive paths.
- **A toolbar on file pages** — **Close**, **Save** and **Reload**. Reload reads the file
  again, after asking when there are unsaved changes; remote files are fetched in the
  background.
- **Position in the status bar** — while a file is open, the line, the column and, with a
  selection, the number of selected characters are in the review field of the status bar.
- **A file type icon for each open file** — chosen by the extension or the name (for
  example `CMakeLists.txt` is a CMake file, `.bashrc` a shell script), from a dark or a light
  set that fits the system appearance. The icons are shipped on all platforms.

### Switching pages is faster

- **`wxSimplebook`** replaces the custom page book, which was slow when switching sessions.
- **The Ctrl+Tab switcher** is modeless: the modal dialog left the application lagging after
  the Ctrl key was released. It also keeps its list border narrower.
- **The recent order** (the Ctrl+Tab order) is updated when you pick a page in the flat list,
  as it is in the tree.
- **Sorting the flat list** keeps the selected page selected.

### On Windows

- **A newer ConPTY** — Kennel ships `conpty.dll` and `OpenConsole.exe` (from the
  Microsoft.Windows.Console.ConPTY NuGet package, MIT) next to `kennel.exe`, also in the
  installer. The ConPTY built into Windows does not pass queries such as `OSC 11 ; ?` to the
  terminal, so apps like Claude Code could not find the background color and picked a dark
  theme on a light one. Without the two files, Kennel falls back to the ConPTY of Windows.
- **Colour emoji and wide characters** — the terminal draws emoji with their own colors
  (with Direct2D), also in the header, and wide characters (CJK, emoji) are no longer
  clipped in the OpenGL renderer.
- **wxWidgets 3.3.4** — `build.sh` builds the `v3.3.4` tag instead of the tip of `master`.

## Additional Functionality

- **Ctrl+Click on a link** — a trailing `.` or `:` is cut from the link text, so
  `src/main.cpp:` opens the file.
- **A Code Reviewer icon** — a teal magnifying glass with a green check mark, readable on dark
  and light themes.
- **`build.sh --cmake`** forces the CMake configure step. `WX_VERSION` selects the
  wxWidgets tag.
- **Terminal and tool changes** — `wxTerminalEmulator` was updated several times: header,
  footer and progress bar strips, OSC 7 / 9;9 working directory, OSC 7501, mouse tracking for
  tmux, and fixes for Shift+Tab and the focus on macOS.

## Bug Fixes

- **Slow tab switching** — see "Switching pages is faster".
- **Block cursor, selection and links on light terminal themes** — see "A theme for the whole
  application".
- **No stripes on a light theme** in the session tree and the flat list.
- **Find on macOS** — Enter now works.

## Under the Hood

- **`ReviewBuddyEvent`** — `ReviewBuddy` sends a custom event to its event target whenever its
  status changes: `STARTED`, `WAITING_FOR_FINDINGS`, `ADDRESSING_FINDINGS`, `FINISHED`,
  `ERROR` (with the reason) and `STOPPED`. The request to close it is a `ReviewBuddyEvent` too
  (`CLOSE`). `wxEVT_REVIEW_CHANGED` is gone. `ReviewBuddyHeader` follows these events and draws
  the header; `ReviewLoop::IsClean()` tells a clean review from the round limit.
- **`toolbar::AddTool()`** and **`toolbar::ApplyStyle()`** (`ToolBarUtils`) add the tools of
  every toolbar with a theme-aware disabled image, and choose the style of a toolbar from the
  appearance of the application.
- **`SearchBar`** is a `wxPanel` subclass, used by files and terminals.
- **`ApplyAppTheme()`** (`AppTheme.h`) applies the saved theme through
  `wxApp::SetAppearance()`.
- **`BlockTimer`** helper in the logger, for measuring slow code.
- `wxTerminalEmulator` now provides `AddHeader()`, `AddFooter()`, `ShowProgressBar()`,
  `GetCurrentDir()` and the working directory event.

## Upgrade Notes

- **Rebuild wxWidgets.** `build.sh` now builds `v3.3.4`. Delete `.build-release/wxWidgets` and
  `.build-release/wxWidgets-install`, and run `./build.sh` again. On Windows, also run CMake
  again with the same generator as before.
- **The default theme is still Dark.** Choose **System** or **Light** in **Settings → Theme**.
- **The terminal has no context menu.** Use the toolbar above the terminal. The shortcut for
  Find is Ctrl+F (Cmd+F on macOS).
- **The Windows installer** now also ships `conpty.dll` and `OpenConsole.exe`.
