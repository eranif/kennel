# Kennel 3.0.0 Release Notes

*Covers changes since tag `2.0.0`.*

Kennel 3.0.0 adds **Find in Files**: search a folder of this computer, of a WSL distribution or
of a host that you reach over ssh, and open a match in the editor with the text selected. The
results are in a new **Output** pane. Kennel installs the search program (ripgrep) on the machine
for you, after it asks. The release also polishes the window that 2.0 introduced: the main
toolbar has a new look and gains an **Archive** button and the shortcut of each button in its
tooltip, the height of the tabs is a setting that you see at once, **Review Buddy** has a menu of
its own and a shortcut, and **Goto Anything** and the page switcher open at the top of the
window, with rounded corners and a shadow.

Highlights:

- **Find in Files** (Ctrl+Shift+F) for this computer, WSL and ssh hosts, with the matches in the
  new **Output** pane (Ctrl+`).
- **ripgrep is installed by Kennel**, the same version everywhere, after you agree.
- A new **main toolbar** with grouped buttons, a new **Archive** button and the shortcut of each
  button in its tooltip.
- A new **Tab Height** setting with four sizes (Short, Small, Medium, Tall), applied live to the
  notebook and to the Side Bar, and available from the right-click menu of the tab bar.
- A **Review Buddy menu** and the shortcut **Ctrl+Shift+R** (Cmd+Shift+R on macOS).
- **Goto Anything** and the page switcher open at the top of the window.
- Find starts with the selected text, also in a terminal.

## Major Improvements

### Find in Files

- **Search a folder of any machine.** **Search → Find in Files...** (Ctrl+Shift+F, also while a
  terminal has the focus) asks what to find, in which folder, and with which options: **Regex**,
  **Match case**, **Whole word**, and the files to **Include** and **Exclude** (globs separated by
  commas, for example `*.cpp, *.hpp`). It works on this computer, in a WSL distribution and on a
  host that runs Linux or macOS and that you reach over ssh.
- **It starts from where you are.** The text selected in the page that is showing is what the
  dialog looks for (like Find does, also in a terminal). The folder is the one of the session. For
  a file it is the **closest workspace**: the working folder of a session, or the folder of its
  file explorer, that holds the file, on the same machine; without one, the folder of the file.
  The home folder of this computer is never taken for a workspace. The pattern, the options, the
  globs and the folder are remembered for the next time.
- **The results are in the Output pane.** **View → Output** (Ctrl+`, Control+` on macOS)
  shows or hides the pane, which has a tab for **Find in Files**. A toolbar has **Stop** and
  **Clear**. The first line says what was searched, where and with which options; the matches
  come grouped by file, with the line number and the matched text in a color of its own; the last
  line says how many matches were found (a search stops at 5000). A search of this computer or of
  WSL shows the matches while it runs; a search of a host over ssh shows them when it ends, and
  **Stop** drops its result. A new search replaces the results.
- **A click opens the match.** One click (or Enter) on a match opens the file at that line, with
  the matched text selected and the line in the middle of the editor. It works for local, WSL and
  ssh files. Nothing in the results can be selected: a click is for opening.

### ripgrep, installed for you

- **Kennel installs ripgrep 15.2.0 on the machine that you search**, into `~/.kennel/bin`, on
  Windows, macOS and Linux, in WSL and on ssh hosts. It is the same version everywhere, and Kennel
  never uses another `rg` that it finds on the machine. A host that runs Windows over ssh is not
  supported.
- **It asks first.** When ripgrep is missing (or is another version), the dialog asks **Download
  and Install ripgrep?** and says where the file goes. Nothing is downloaded or written before you
  say yes; **No** closes the dialog.
- **The download is checked.** It comes from the ripgrep release on GitHub and is compared with a
  SHA-256 checksum that is part of Kennel. A file that does not match is thrown away and nothing
  is written. The program is written under another name and renamed, so a failed install never
  leaves a file that looks installed. A remote machine needs no internet: this computer
  downloads the file and sends it.

### The file explorer follows a file without waiting

- **A file that you open is shown in the file explorer at once.** The folders on the way that are
  not listed yet are listed on a thread of their own, so a click on a match in a folder that was
  not open no longer freezes the window (a big folder, a WSL share). Only the file that is
  showing is selected, and it is scrolled into view.
- **Close All Files is lighter.** The file explorer does not follow every page that shows while
  the files close one after the other.

### The main toolbar

- **A new look.** The toolbar on the left of the window is drawn to match the tab bars: a flat
  bar in the color of the theme, with the buttons of a group sitting together on a rounded pill
  that looks sunk into the bar, and a gap between groups. Tools are larger, hovered and pressed
  buttons are highlighted with a circle, and a button with a drop-down has its arrow under the
  icon. It is the same toolbar on every platform: on macOS it is no longer drawn by the generic
  `wxToolBar`.
- **An Archive button.** It opens **Archived Sessions**, where you restore or delete the sessions
  you have archived. It sits in a group with **Manage Jobs**, between the buttons that start a
  session and the buttons of your agents. The shortcut is **Ctrl+Shift+A** (Cmd+Shift+A on
  macOS), also while a terminal has the focus.
- **Tooltips show the shortcut.** A button of the main toolbar that has a keyboard shortcut shows
  it on its own line below its name, for example **Start a New Agent** and **Ctrl+T**. That is
  **Start a New Agent** (Ctrl+T), **Start a New Terminal** (Ctrl+E) and **Archive**
  (Ctrl+Shift+A); **Manage Jobs** and the buttons of the agents have no shortcut. The shortcut is
  the one of the menu item that does the same, so there is one place that says what the key is.
- The toolbars at the top of a session and of a file keep a plain background.

### Tab height

- **A new setting, Settings → Tab Height**, with **Short**, **Small**, **Medium** and **Tall**.
  The choice is applied while you pick it, and **Cancel** puts the previous height back.
- **The Side Bar follows.** The tabs of the File Explorer and of the Git view have the same
  height as the tabs of the notebook.
- **The right-click menu of the tab bar has a Tab Height submenu**, next to **Tab Position**. A
  choice there is saved and applied to both tab bars.
- The four heights have the same size on every platform: the padding of a tab is 4, 6, 8 and 10
  DIPs. See "Upgrade Notes".

### Review Buddy

- **A Review Buddy menu** in the menu bar, between **Jobs** and **Settings**. It is there only
  while an agent session is showing: not for a terminal and not for a file. It has **Start Review
  Buddy...**, what the review buddy is doing, **Send the Request Again**, **Stop the Review**,
  **Open the Latest Review** and **Close Review Buddy**.
- **Ctrl+Shift+R** (Cmd+Shift+R on macOS) starts a review buddy, also while the agent's terminal
  has the focus. On Windows and Linux, where the key can not be used (a terminal, a scheduled
  job, a review buddy that is open) it goes on to the terminal.
- **Start Review Buddy...** shows a list of the agents that can review the session, instead of
  one menu entry for each agent. When there is only one, the review starts at once. If a review
  can not be started (the folder is not a git repository, no agent runs on the same host as the
  session), a message says why.
- The Review Buddy button is no longer in the toolbar of a plain terminal. The menu of the button
  opens below it, and no longer covers it.

### Goto Anything and the page switcher

- **Both open at the top of the window**, centered, with rounded corners and a shadow (on
  Windows and macOS).
- **The page switcher is a popup.** It takes half the width and a quarter of the height of the
  window, and does not take the activation from it. It closes when you release Ctrl, press
  Escape, or click outside of it.
- **Goto Anything** is a dialog styled like that popup, placed just below the title bar of the
  window.
- On macOS the popups are translucent, with a blurred background.
- The shortcuts in Goto Anything are shown with the names of the keys of the platform (for
  example Cmd on macOS).

### Find

- **Find starts with the selected text.** When you press Ctrl+F (Cmd+F on macOS), the search box
  starts with the text you selected in the editor or in the terminal. A selection of many lines
  gives its first line that has text. Before, a selection of many lines was ignored.
- **In a terminal, the text that a full-screen program selected comes first.** Such a program
  selects text itself and copies it with an OSC 52 sequence; the terminal does not know what it
  selects. The text selected with the mouse comes next.

### Other improvements

- **OSC 52 copy requests are handled.** A program in the terminal can copy text to the clipboard.
  The copy is done only while that terminal has the keyboard focus, so a program in a tab in the
  background can not change your clipboard.
- **Diffs have an icon.** The tabs of the diffs of the Git view (`.diff`, `.staged.diff`) and
  the files with the extensions `diff` and `patch` show an icon of their own: a green plus and a
  red minus.
- **The context menu of the Git view starts with Open File**, then a separator, then the other
  entries. For a single file it also has **Open with Default App** (not for a file on a remote
  host, which this computer can not open).
- **The captions of the side panes** use the colors of the terminal theme and follow it when it
  changes.
- **On macOS** the tab bar of the notebook shows a blurred view of the desktop where it has no
  tab.
- **The Job dialog** has a tidier layout: **Keep terminal open** and **Enabled** are on one
  line, and the extra separator is gone.
- **Switching the Side Bar between sessions is lighter.** It selects a page of the File Explorer
  and the Git view only when it is not the one showing, and holds the window for the two switches
  only, not for the git refresh and the remote connection that follow.

## Fixes

- **WSL: the home folder was not found.** A path that starts with `~` in a WSL distribution could
  not be resolved, because the command that asks the distribution for its home folder was started
  without quotes and printed nothing.
- **Windows: folders and names that are not ASCII.** Kennel starts programs with their
  command line in UTF-16, so a folder such as `C:\Users\José\project` or `D:\проект` is passed
  correctly to `git` and to the other programs that Kennel runs. Before, the system code page
  could garble it.
- **macOS build:** Kennel links the Cocoa framework that the new popups need, and the build no
  longer prints the `NSOpenGLContext` deprecation warning of the wxWidgets headers.

## Internal Changes

- **`KennelToolBarArt`** draws the main toolbar (and the horizontal toolbars of the pages with a
  compact variant). `toolbar::ApplyStyle()` leaves the background of a toolbar with this art as
  it was made.
- **`wxEVT_APP_TAB_HEIGHT`** (in `core/AppManager.h`) asks the tab bars to show a tab height. The
  settings dialog and the menu of the tab bar send it; the notebook and the Side Bar listen.
  `TabPadding.h` has the padding of each height. In the prefs file `tabHeight` keeps its numbers
  (0 Short, 1 Medium, 2 Tall) and Small is 3, so a saved choice keeps its meaning.
- **`ReviewBuddyMenu`** is the Review Buddy menu, made like the Edit menu: it goes into the menu
  bar and comes out as the page changes.
- **`StylePopupWindow()`** gives a popup its shadow and rounded corners (Windows 11, macOS), and
  `SetGlassBackground()` draws the blurred background on macOS.
- **`Process::QuoteArgument()`** quotes an argument for a Windows command line. `Process` starts
  programs with `CreateProcessW`.
- **Find in Files:** `FindInFiles` searches with ripgrep (this machine, WSL, ssh) and numbers its
  searches, so a late answer of a replaced search is ignored. `FindInFilesDlg` is the dialog and
  `FindResultsView` the results. `Ripgrep` and `ToolDescription` pin the release and its SHA-256
  checksums for Linux, macOS and Windows (`.zip` assets, read by `ZipArchive`; `.tar.gz` by
  `TarGz`).
- **`RemoteInstaller`** puts one tool of a `ToolDescription` into a folder of an account: it asks
  the machine what it is, checks the installed version, downloads once here, checks the checksum,
  and uploads it, without blocking the UI thread. It is GUI free, works for any tool and folder,
  and has a check-only mode so that the caller can ask the user first.
- **`OutputView`** is the Output pane: a `wxAuiNotebook` with the `GlyphTabArt` of the
  application and one tab for each kind of output (`SelectTab()`).
- **`SearchablePage`** is the interface of the pages that have a search (`CanFind()`,
  `ShowFind()`, `FindNext()`, `GetFindWhat()`, `GetSearchTarget()`); `FilePage` and `SessionPage`
  implement it, and `MainNotebook` forwards the Find commands to the page that is showing.
- **`FileEvent`** carries a line, a column and a length (a match to select), and
  `wxEVT_CLOSE_ALL_FILES_STARTED` / `_ENDED` surround Close All Files. `FileTreeCtrl` lists the
  folders that a reveal needs on one joinable thread, from a queue that replaces a folder that is
  queued already.
- **`UiPrefs`** keeps the last Find in Files (pattern, options, globs, folder).
- **`AGENTS.md`** says how agents work with the user.
- **wxCrafter files** are changed through `UI.wxcp` and `wxcgen`; `CLAUDE.md` describes how.
- The version number is 3.0.0.

## Upgrade Notes

- **The tabs may have another height than in 2.0.** The default is Medium on Windows and macOS
  and Short on Linux. Medium is 8 DIPs of padding, a little more than the 6 of 2.0, and on Linux
  Short is 4 DIPs, tighter than the 10 of 2.0. Pick another height in **Settings → Tab Height**
  or from the right-click menu of the tab bar.
- **The Side Bar tabs follow the tab height** of the notebook.
- **New shortcuts:** Ctrl+Shift+A (Archived Sessions), Ctrl+Shift+R (Start Review Buddy),
  Ctrl+Shift+F (Find in Files) and Ctrl+` (the Output pane; Control+` on macOS). Programs in a
  terminal no longer get these keys while Kennel handles them.
- **The Review Buddy button is not in the toolbar of a terminal.** An agent session still has
  it, and so has the new Review Buddy menu.
- **The first search on a machine downloads ripgrep.** This computer needs internet for it, once
  for each machine that you search, and Kennel asks before it does. The file goes into
  `~/.kennel/bin` of that machine.
- **The pane layout:** the Output pane is hidden when Kennel starts, and a layout that was saved
  with it showing is restored with it hidden.
