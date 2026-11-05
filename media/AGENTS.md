# Media: screenshots and the promo video

This folder holds the real screenshots of Kennel and the promo video (a 60 second animated
page built from them). This file says how to make them again for a new release.

```
media/
  AGENTS.md              this file
  screenshots/*.jpg      real screenshots of the running app (used by README.md and the promo)
  promo/build.py         builds promo/kennel-promo.html (scenes, captions, timings, camera)
  promo/kennel-promo.html  the promo: one HTML file, the screenshots are inside (about 3 MB)
  promo/kennel-promo.mp4   a recording of the promo, for README.md (about 6 MB)
  promo/teaser.gif       the 18 second animated teaser at the top of README.md (links to the video)
  demo/setup-demo.sh     makes the demo environment (fake agents, git repos, config)
  demo/capture.sh        one screenshot of the real app
  demo/capture-all.sh    all the screenshots, saved in screenshots/
  demo/record-promo.sh   records the promo page and writes promo/kennel-promo.mp4
  demo/trim-video.swift  cuts and encodes the recording (macOS has no ffmpeg)
  demo/frames.swift      prints frames of a video as PNG files, to check it
  demo/winpos.js         finds the window to capture
  demo/park-mouse.js     moves the mouse pointer out of the recording
  demo/demo-hook.patch   a temporary patch: opens dialogs for a screenshot (never commit it)
```

## What is real and what is not

- Every picture of the app is a **real screenshot** of the built app, taken by `screencapture`.
- The **content** of the sessions comes from one of two modes of `setup-demo.sh`:
  - **Real agents (`DEMO_REAL=1`, what the 2.0 pictures use):** `claude` (two sessions) and
    `opencode` run for real, with a cheap model and a first prompt that depends on the project
    (see the wrappers in `setup-demo.sh`). The wrappers run with your real `HOME`, so the tools are
    logged in; Kennel itself still runs with the throw-away `HOME`. Each launch resets the repo
    (`git stash`) so the work is the same every time. Codex is not used.
    Before the first run, trust the projects once: `cd /tmp/k && claude` (and the same in
    `/tmp/k/docs`), choose "Yes, I trust this folder". The history of these runs lands in your real
    `~/.claude` and `~/.local/share/opencode`.
  - **Fake agents (default):** small scripts that print an agent-like screen and report a status.
    They are named like the real tools, so Kennel treats them as the real ones.
  - In both modes `kiro-cli` is a script: it is the **blocked** session (an orange pulse needs an
    agent that waits at an exact moment) and the Review Buddy reviewer (the real one shows upgrade
    banners and an approval prompt).
  The demo never touches your real `~/.kennel`.
  **Look at every real-agent picture before you commit it:** banners can show an account, an email,
  a user name or a real path (the real runs showed the plan name only, no email). The capture takes
  about 10 minutes: `DEMO_WAIT=40 ./media/demo/capture-all.sh` (seconds to wait after each start).

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
DEMO_REAL=1 ./media/demo/setup-demo.sh   # (without DEMO_REAL: fake agents) + 4 git repos in /tmp/k + HOME in /tmp/kennel-demo
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

The hook has more modes: `start` (the Start Agent dialog) and `wizard` (the New Agent Wizard).
`capture-all.sh` cuts these two dialogs out of the screen, so close other windows first (a window
of another application behind the dialog shows at its edge).

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

### Record the video

README.md links to `promo/kennel-promo.mp4`, a recording of the page. Make it after the page is final:

```bash
./media/demo/record-promo.sh      # takes about 80 seconds; writes media/promo/kennel-promo.mp4
```

It opens the page in Chrome in its own window (`#rec@<time>`: no controls, and the clock starts at
a given time while the page shows its first frame, so the recording can start before it), records
that rectangle of the screen with `screencapture -v`, and cuts and encodes it with
`demo/trim-video.swift` (H.264, 1920x1080, 3000 kbit/s: about 21 MB; the screen is captured at 2x, so the text stays sharp). While it runs: do not touch the
mouse or the keyboard, and close windows with private content that could show if the window moves
(the first version of the video had an IDE in its first frame, because the recording started before
the window was up: the script now waits for the window).

Look at the video before you commit it. This prints frames at the given seconds as PNG files:

```bash
swift media/demo/frames.swift media/promo/kennel-promo.mp4 /tmp/frame 0 10 30 58   
```

`demo/frames.swift` uses `AVAssetImageGenerator` (one PNG per time). 
### The teaser GIF

GitHub does not play a video from the repository inside a README, but it shows a GIF. Make the
teaser from the finished video (a few clips, 720 pixels wide, 8 frames per second, about 3 MB):

```bash
# Look at the video first (every 1.5 s) to find the times of the highlights, then:
swift media/demo/make-gif.swift media/promo/kennel-promo.mp4 media/promo/teaser.gif 720 8 \
  9.0-10.8 15.0-17.4 22.4-23.8 24.2-26.0 33.3-38.8 43.0-44.8 46.0-47.2 49.0-50.8
```

The times of the video are not the times of the page (the recording starts before the page does):
find them in the video, with `demo/frames.swift`, not in `build.py`.

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
