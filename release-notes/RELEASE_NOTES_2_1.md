# Kennel 2.1.0 Release Notes

*Covers changes since tag `2.0.0`.*

Kennel 2.1.0 polishes the window that 2.0 introduced. The main toolbar has a new look and gains
an **Archive** button, every toolbar button shows its keyboard shortcut, and the height of the
tabs is now a setting you can change from the right-click menu of the tab bar and see at once.
**Review Buddy** has a menu of its own and a shortcut. **Goto Anything** and the page switcher
open as popups at the top of the window.

Highlights:

- A new **main toolbar** with grouped buttons, a new **Archive** button and the shortcut of each
  button in its tooltip.
- A new **Tab Height** setting with four sizes (Short, Small, Medium, Tall), applied live to the
  notebook and to the Side Bar, and available from the right-click menu of the tab bar.
- A **Review Buddy menu** and the shortcut **Ctrl+Shift+R** (Cmd+Shift+R on macOS).
- **Goto Anything** and the page switcher as popups.
- Find starts with the selected text, also in a terminal.

## Major Improvements

### The main toolbar

- **A new look.** The toolbar on the left of the window is drawn to match the tab bars: a flat
  bar in the color of the theme, with the buttons of a group sitting together on a rounded pill
  that looks sunk into the bar, and a gap between groups. Tools are larger, hovered and pressed
  buttons are highlighted with a circle, and a button with a drop-down has its arrow under the
  icon. It is the same toolbar on every platform (on macOS it is no longer the native toolbar).
- **An Archive button.** It opens **Archived Sessions**, where you restore or delete the sessions
  you have archived. It sits in a group with **Manage Jobs**, between the buttons that start a
  session and the buttons of your agents. The shortcut is **Ctrl+Shift+A** (Cmd+Shift+A on
  macOS), also while a terminal has the focus.
- **Tooltips show the shortcut.** A button that has a keyboard shortcut shows it on its own line
  below its name, for example **Start a New Agent** and **Ctrl+T**. The shortcut is the one of the
  menu item that does the same, so there is one place that says what the key is.
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
  has the focus. Where it can not be used (a terminal, a scheduled job, a review buddy that is
  open) the key goes on to the terminal.
- **Start Review Buddy...** shows a list of the agents that can review the session, instead of
  one menu entry for each agent. When there is only one, the review starts at once. If a review
  can not be started (the folder is not a git repository, no other agent runs on the same
  host), a message says why.
- The Review Buddy button is no longer in the toolbar of a plain terminal. The menu of the button
  opens below it, and no longer covers it.

### Goto Anything and the page switcher

- **Both are popups** that do not take the activation from the window. They open at the top of
  the window, centered, with rounded corners and a shadow (Windows 11, macOS). The page switcher
  closes when you release Ctrl, press Escape, or click outside of it.
- On macOS the popups are translucent, with a blurred background, and the lists have no row
  lines.
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
  The copy is done only when that terminal is the one you are looking at.
- **Diffs have an icon.** The tabs of the diffs of the Git view (`.diff`, `.staged.diff`) and
  the files with the extensions `diff` and `patch` show an icon of their own: a green plus and a
  red minus.
- **The context menu of the Git view starts with Open File**, then a separator, then the other
  entries. It also has **Open with Default App**.
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

- **Windows: folders and names that are not ASCII.** Kennel starts programs with their
  command line in UTF-16, so a folder such as `C:\Users\José\project` or `D:\проект` is passed
  correctly to `git` and to the other programs that Kennel runs. Before, the system code page
  could garble it.
- **macOS:** Kennel links the Cocoa framework that the new popups need, and the build no longer
  prints the `NSOpenGLContext` deprecation warning of the wxWidgets headers.

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
- **Groundwork for Find in Files** (no user interface yet): a search with ripgrep for this
  machine, WSL and remote hosts, with a pinned ripgrep release and its SHA-256 checksums, and the
  helpers to check and unpack a download (`Sha256`, `TarGz`, `ToolDescription`, `Ripgrep`,
  `FindInFiles`).
- **wxCrafter files** are changed through `UI.wxcp` and `wxcgen`; `CLAUDE.md` describes how.
- The version number is 2.1.0.

## Upgrade Notes

- **The tabs may have another height than in 2.0.** The default is Medium on Windows and macOS
  and Short on Linux. Medium is 8 DIPs of padding, a little more than the 6 of 2.0, and on Linux
  Short is 4 DIPs, tighter than the 10 of 2.0. Pick another height in **Settings → Tab Height**
  or from the right-click menu of the tab bar.
- **The Side Bar tabs follow the tab height** of the notebook.
- **New shortcuts:** Ctrl+Shift+A (Archived Sessions) and Ctrl+Shift+R (Start Review Buddy).
  Programs in a terminal no longer get these two keys while Kennel handles them.
- **The Review Buddy button is not in the toolbar of a terminal.** An agent session still has
  it, and so has the new Review Buddy menu.
