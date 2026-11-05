# Media: screenshots and the promo video

This folder holds the real screenshots of Kennel and the promo video (a 60 second animated
page built from them). This file says how to make them again for a new release.

```
media/
  AGENTS.md              this file
  screenshots/*.jpg      real screenshots of the running app (used by README.md and the promo)
  promo/build.py         builds promo/kennel-promo.html (scenes, captions, timings, camera)
  promo/kennel-promo.html  the promo: one HTML file, the screenshots are inside (about 3 MB)
  demo/setup-demo.sh     makes the demo environment (fake agents, git repos, config)
  demo/capture.sh        one screenshot of the real app
  demo/capture-all.sh    all the screenshots, saved in screenshots/
  demo/winpos.js         finds the window to capture
  demo/demo-hook.patch   a temporary patch: opens dialogs for a screenshot (never commit it)
```

## What is real and what is not

- Every picture of the app is a **real screenshot** of the built app, taken by `screencapture`.
- The **content** in the sessions is fake: the agents are small scripts that print an agent-like
  screen and report a status. They are named like the real tools (`claude`, `codex`,
  `kiro-cli`, `opencode`), so Kennel treats them as the real ones. The demo does not call any
  AI service, and it never touches your real `~/.kennel` (it runs with another `HOME`).
- The only drawn thing in the promo is the "4 sessions restored" card of the resume scene
  (search for `sessions restored` in `promo/build.py`). Say so if you add more.

## Requirements

- macOS (the scripts use `open`, `screencapture`, `sips`, `osascript`).
- **Screen Recording permission** for the terminal (or the agent) that runs `capture.sh`.
  Without it `screencapture` fails with "could not create image from display".
- A build of Kennel in `.build-release/kennel.app` (`./build.sh`).
- `python3`, `git`, and Google Chrome (only to check the promo).
- macOS does not let a script click or type in another app without the Accessibility
  permission. That is why the dialogs are opened by the demo hook, not by key presses.

## 1. Make the screenshots

```bash
./media/demo/setup-demo.sh            # fake agents + 4 git repos in /tmp/k + HOME in /tmp/kennel-demo
git apply media/demo/demo-hook.patch  # TEMPORARY: lets the demo open the dialogs
./build.sh
./media/demo/capture-all.sh           # writes media/screenshots/*.jpg (takes about 2 minutes)
git apply -R media/demo/demo-hook.patch && ./build.sh   # take the hook out again
```

Do not touch the mouse or the keyboard while it runs, and keep the window of Kennel in front:
the script captures a rectangle of the screen when a dialog is open.

What the hook does (`KENNEL_DEMO=<mode>`, four seconds after the start): `switch` opens the
Ctrl+Tab dialog (and keeps it open without Ctrl held), `goto` opens Goto Anything with the text
of `KENNEL_DEMO_TEXT`, `explorer` selects the File Explorer tab of the side bar, `review` starts
Review Buddy with the agent `kiro-cli`. The patch only touches `src/app/` and is marked
`DEMO-TMP`. **Check `git status` before a commit: the hook must not be in it.**

Look at every picture (open the files) before you go on. Things that went wrong before:

| Problem | Cause and fix |
|---|---|
| Dim text that is hard to read | The "bright black" of the terminal theme is almost the background of Mariana. The fake agents use `38;5;249` for dim text. Keep that. |
| An agent shows a spinner, not the pulse | A resume flag made the fake script take the wrong branch. `kiro-cli` is the reviewer only when an argument is longer than 40 characters (the review prompt). |
| A long temp path in the status line | The projects are in `/tmp/k` on purpose: the status line shows the folder. |
| The window is grey (inactive) | The app was not in front. Use `capture.sh` (it starts the app with `open`, which brings it to the front). |
| A dialog is missing | The hook did not run: `git apply` it, rebuild, and check that the build is the one in `.build-release/kennel.app`. |
| The first-start scan shows nothing | Take it right after the start (the scan takes less than a second). `capture-all.sh` uses 1 second. |

To capture another state, copy a `shot` line in `capture-all.sh`. `capture.sh` takes the session
that is selected at the start (`api-refactor`, `docs-site`, `infra`, `web`). Statuses come from the
fake agents: they print an OSC 7501 report (`state=working:progress=40`, `done`, `blocked`, `idle`),
see `setup-demo.sh`.

## 2. Build the promo

```bash
python3 media/promo/build.py     # writes media/promo/kennel-promo.html
open media/promo/kennel-promo.html
```

The page is a 1280x720 stage scaled to the window. Each scene is a real screenshot with a
virtual camera (`cam(...)`: key frames `time:centerX,centerY,zoom`), highlight boxes (`spot`s, in
percent of the screenshot), a caption and optional key caps. Everything is a function of the time,
so the slider can seek. The scenes are listed in `promo/build.py` after `var scenes = [];`:

1. Title - 2. First start (agent scan) - 3. One window (Ctrl+Tab) - 4. Working, done, blocked -
5. Resume - 6. File Explorer and git follow the session - 7. Review Buddy - 8. Goto Anything -
9. Terminal - 10. Outro. The total is 60 seconds: if you change a scene length, change the
`TOTAL` and the slider `max` too.

Rules that kept it good:
- A caption must not cover what a highlight box shows. The caption has `right` (bottom right) and a
  `flip` time (moves to the top left); see the Review Buddy scene.
- Zoom only as far as needed. The camera is clamped to the screenshot, so a point near an edge
  cannot be centred.
- Say what is real. Keep the captions short and true (no feature that is not in the release).

### Check the promo without a screen

Chrome can render a frame at a given time (the `#t<seconds>` hash stops the clock and removes
the fades):

```bash
"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --headless=new --disable-gpu \
  --screenshot=/tmp/frame.png --window-size=1300,820 "file://$PWD/media/promo/kennel-promo.html#t44"
```

Look at one frame per scene. For a JavaScript error add `--enable-logging=stderr --v=0 --dump-dom`
and search the output for `CONSOLE`.

## 3. Release checklist

1. Build the release, run `setup-demo.sh`, make the screenshots, take the hook out (above).
2. Check that the version in the status line of the screenshots is the new one.
3. Update the captions of the promo and the "Screenshots" section of `README.md` for what is new.
4. Build the promo, check one frame per scene, then publish the HTML (it is one file).
5. Commit `media/` (sign off, no `Co-Authored-By:`). The screenshots are about 2.3 MB: do not add
   more than needed. Re-use the file names, so the README links stay valid.
