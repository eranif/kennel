"""Builds media/promo/kennel-promo.html: the promo video (a 60 second animated page).

The page embeds the screenshots of ../screenshots/*.jpg. Run:  python3 build.py
The scenes, captions, timings and camera moves are in the JavaScript template below
(search for "var scenes"). See ../AGENTS.md.
"""
import base64, os
D = os.path.dirname(os.path.abspath(__file__))
SHOTS = os.path.join(D, "..", "screenshots")

def b64(n):
    return "data:image/jpeg;base64," + base64.b64encode(open(f"{SHOTS}/{n}.jpg", "rb").read()).decode()

HTML = r'''<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Kennel Promo Video</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Bricolage+Grotesque:wght@600;800&display=swap" rel="stylesheet">
<style>
/* Layout idea: a fixed 1280x720 stage scaled to fit. Real screenshots of Kennel fill it; a virtual camera
   zooms and pans over them, with a lower-third caption. Everything is a pure function of time (seekable). */
:root {
  --ink: #15181b;
  --line: #2d3338;
  --text: #e9edf1;
  --dim: #9aa5b0;
  --accent: #6699cc;
  --amber: #f59e0b;
  --display: "Bricolage Grotesque", "Avenir Next", "Segoe UI", sans-serif;
  --ui: -apple-system, BlinkMacSystemFont, "SF Pro Text", "Segoe UI", sans-serif;
  color-scheme: dark;
}
* { box-sizing: border-box; margin: 0; }
html, body { height: 100%; background: var(--ink); color: var(--text); font-family: var(--display); }
body { display: flex; flex-direction: column; align-items: center; justify-content: center; gap: 12px; padding: 16px; }
#frame { width: 100%; max-width: 1280px; aspect-ratio: 16 / 9; position: relative; overflow: hidden; border-radius: 10px; background: #000; border: 1px solid var(--line); }
#stage { position: absolute; left: 0; top: 0; width: 1280px; height: 720px; transform-origin: 0 0; overflow: hidden; background: var(--ink); }

.scene { position: absolute; inset: 0; opacity: 0; visibility: hidden; transition: opacity .6s; }
.scene.on { opacity: 1; visibility: visible; }
.r { opacity: 0; transform: translateY(12px); transition: opacity .5s, transform .5s; }
.r.in { opacity: 1; transform: none; }
.out { opacity: 0 !important; }
.pop { opacity: 0; transform: scale(.96); transition: opacity .25s, transform .25s; }
.pop.in { opacity: 1; transform: none; }
.fade { opacity: 0; transition: opacity .5s; }
.fade.hl { opacity: 1; }
.chip[data-hl] { opacity: .5; } .chip[data-hl].hl { opacity: 1; border-color: var(--accent); color: #9fc3e8; }

/* the camera: a 1280x722 screenshot that is moved and scaled */
.cam { position: absolute; left: 0; top: 0; width: 1280px; height: 722px; transform-origin: 0 0; }
.cam img { position: absolute; inset: 0; width: 100%; height: 100%; display: block; }
.spot { position: absolute; border: 3px solid var(--accent); border-radius: 6px; box-shadow: 0 0 0 9999px rgba(8, 10, 12, .0), 0 0 18px var(--accent); opacity: 0; transition: opacity .4s; }
.spot.in { opacity: 1; animation: glow 1.4s ease-in-out infinite; }
.spot.out { opacity: 0 !important; }
.spot.amber { border-color: var(--amber); box-shadow: 0 0 18px var(--amber); }
@keyframes glow { 50% { filter: brightness(1.5); } }

.cap { position: absolute; left: 40px; bottom: 34px; max-width: 760px; padding: 18px 24px 20px; border-radius: 12px; background: rgba(12, 15, 18, .86); border: 1px solid #2d3640; backdrop-filter: blur(6px); }
.cap.right { left: auto; right: 40px; }
.cap.flip { bottom: auto; top: 34px; left: 40px; right: auto; }
.cap .kick { font: 700 13px var(--ui); letter-spacing: .1em; text-transform: uppercase; color: var(--accent); margin-bottom: 6px; }
.cap h2 { font-weight: 800; font-size: 38px; line-height: 1.05; letter-spacing: -.02em; text-wrap: balance; }
.cap p { margin-top: 8px; font: 500 18px/1.35 var(--ui); color: var(--dim); }

.panel { position: absolute; left: 50%; top: 50%; width: 520px; margin-left: -260px; margin-top: -120px; background: #2b2b2b; border: 1px solid #4a4a4a; border-radius: 10px; padding: 6px; box-shadow: 0 20px 50px #000b; z-index: 5; font-family: var(--ui); }
.panel .row { display: flex; align-items: center; gap: 10px; height: 34px; padding: 0 10px; border-radius: 6px; font-size: 14px; color: #eaeaea; }
.panel .row small { color: #9a9a9a; margin-left: auto; font-size: 12px; }
.panel .row.hl { background: #0a60d0; color: #fff; }
.panel .row.hl small { color: #cfe0ff; }
.dot { width: 10px; height: 10px; border-radius: 3px; flex: none; }
.key { display: inline-block; font: 600 16px var(--ui); padding: 7px 13px; border-radius: 8px; border: 1px solid #4a5360; border-bottom-width: 3px; background: #20262c; color: var(--text); }
.chip { display: inline-flex; align-items: center; gap: 8px; padding: 8px 14px; border: 1px solid var(--line); border-radius: 999px; font: 600 15px var(--ui); background: #1b2025; color: var(--text); }
.chip.on2 { border-color: var(--accent); color: #9fc3e8; }
.row-chips { display: flex; gap: 12px; flex-wrap: wrap; justify-content: center; }
.center { position: absolute; inset: 0; display: flex; flex-direction: column; align-items: center; justify-content: center; text-align: center; gap: 22px; background: var(--ink); }
.logo { width: 108px; height: 108px; }
.big { font-weight: 800; font-size: 120px; letter-spacing: -.04em; line-height: 1; }
.tag { font: 500 28px var(--ui); color: var(--dim); max-width: 820px; text-wrap: balance; }
.mono { font: 15px/1.6 ui-monospace, "SF Mono", Menlo, monospace; color: #d8dee9; }
.mono .y { color: #f9ae58; } .mono .b { color: #6699cc; } .mono .g { color: #99c794; } .mono .d { color: #8d99a6; }
.card { position: absolute; background: #2b2b2b; border: 1px solid #4a4a4a; border-radius: 10px; box-shadow: 0 20px 50px #000b; font-family: var(--ui); }
.find { margin: 6px 0; padding: 7px 12px; border-left: 3px solid #f9ae58; background: #343d46; border-radius: 3px; font: 13.5px ui-monospace, "SF Mono", Menlo, monospace; color: #d8dee9; }
.find b { color: #f9ae58; font-weight: 600; }
.prog { position: relative; height: 4px; background: #3a4450; border-radius: 2px; overflow: hidden; margin: 8px 0 10px; }
.prog i { display: block; height: 100%; width: 0; background: #99c794; }

#ctl { width: 100%; max-width: 1280px; display: flex; align-items: center; gap: 12px; font: 600 14px var(--ui); color: var(--dim); }
#ctl button { background: #20262c; color: var(--text); border: 1px solid var(--line); border-radius: 8px; padding: 8px 14px; font: 600 14px var(--ui); cursor: pointer; }
#ctl button:focus-visible, #seek:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
#seek { flex: 1; accent-color: var(--accent); }
@media (max-width: 480px) { #time { display: none; } }
</style>
</head>
<body>
<div id="frame"><div id="stage"></div></div>
<div id="ctl">
  <button id="play" type="button">Pause</button>
  <button id="restart" type="button">Restart</button>
  <input id="seek" type="range" min="0" max="60" step="0.05" value="0" aria-label="Seek">
  <span id="time">0:00 / 1:00</span>
</div>

<script>
(function () {
  var TOTAL = 60;
  var stage = document.getElementById('stage');
  var IMG = { api: "@@api@@", docs: "@@docs@@", infra: "@@infra@@", web: "@@web@@", sw: "@@switch@@", go: "@@goto@@", xa: "@@explorer_api@@", xd: "@@explorer_docs@@", rv: "@@review@@", scan: "@@scan@@" };

  // camera keyframes: "t:cx,cy,s;..." (cx, cy: the point of the screenshot, 0..1, that goes to the middle of the stage)
  function cam(frames, cam, spots) {
    var h = '<div class="cam" data-cam="' + cam + '">';
    frames.forEach(function (f) { h += '<img class="' + (f[1] ? 'fade' : '') + '" ' + (f[1] ? 'data-hl="' + f[1] + '"' : '') + ' src="' + IMG[f[0]] + '" alt="">'; });
    (spots || []).forEach(function (s) { h += '<div class="spot ' + (s.c || '') + '" data-at="' + s.at + '"' + (s.until ? ' data-until="' + s.until + '"' : '') + ' style="left:' + s.x + '%;top:' + s.y + '%;width:' + s.w + '%;height:' + s.h + '%"></div>'; });
    return h + '</div>';
  }
  function caption(kick, h, p, right, flip) { return '<div class="cap r' + (right ? ' right' : '') + '" data-at="0.2"' + (flip ? ' data-flip="' + flip + '"' : '') + '><div class="kick">' + kick + '</div><h2>' + h + '</h2><p>' + p + '</p></div>'; }
  function typed(text, at, cps) { return '<span data-type="' + text + '" data-at="' + at + '" data-cps="' + (cps || 30) + '"></span>'; }

  var scenes = [];
  function keys(list, at, until, pos) { return '<div class="r" data-at="' + at + '"' + (until ? ' data-until="' + until + '"' : '') + ' style="position:absolute;' + (pos || 'right:44px;top:34px') + ';display:flex;gap:8px;align-items:center;z-index:7">' + list.map(function (k) { return '<span class="key">' + k + '</span>'; }).join('') + '</div>'; }

  // 0. Title (5s)
  scenes.push({ d: 5, html:
    '<div class="center">' +
    '<svg class="logo r" data-at="0" viewBox="0 0 112 112"><rect width="112" height="112" rx="26" fill="#6699cc"/><g fill="#12202e"><ellipse cx="56" cy="70" rx="22" ry="18"/><ellipse cx="31" cy="48" rx="9" ry="12"/><ellipse cx="81" cy="48" rx="9" ry="12"/><ellipse cx="45" cy="33" rx="8" ry="11"/><ellipse cx="67" cy="33" rx="8" ry="11"/></g></svg>' +
    '<div class="big r" data-at="0">Kennel</div>' +
    '<div class="tag r" data-at="0.5">A workspace for AI CLI agents, terminals and files</div>' +
    '<div class="row-chips r" data-at="1.2">' + ['Claude Code', 'OpenAI Codex', 'Kiro CLI', 'OpenCode', 'Your own tool'].map(function (n) { return '<span class="chip">' + n + '</span>'; }).join('') + '</div></div>' });

  // 1. Auto-scan on the first start (6s)
  scenes.push({ d: 6, html:
    cam([['scan', '']], '0:0.5,0.5,1;1:0.5,0.5,1;2.4:0.06,0.2,3.2;4:0.06,0.2,3.2;5:0.1,0.97,3;6:0.1,0.97,3',
      [{ x: 0.0, y: 12.5, w: 1.6, h: 11.5, at: 2.6, until: 4.6, c: 'amber' }, { x: 0.3, y: 97.7, w: 15, h: 2.0, at: 5.2 }]) +
    caption('First start', 'Kennel finds your agents', 'On the first start it scans this computer, and optionally WSL and SSH hosts, and defines every agent it finds: Claude, Codex, Kiro and OpenCode.', true) });

  // 2. One window + the real Ctrl+Tab switcher (7s)
  scenes.push({ d: 7, html:
    cam([['api', ''], ['sw', '2.6-99']], '0:0.5,0.5,1;2:0.5,0.5,1;3.2:0.5,0.5,2.1;7:0.5,0.5,2.1') +
    caption('One window', 'Every agent, side by side', 'Sessions, terminals and files are tabs of one notebook, grouped by session group. Ctrl+Tab switches in most-recently-used order.') +
    keys(['Ctrl', 'Tab'], 2.2, null) });

  // 3. Status (7s)
  scenes.push({ d: 7, html:
    cam([['infra', '']], '0:0.5,0.5,1;1:0.5,0.5,1;2:0.29,0.07,3;3.6:0.29,0.07,3;4.4:0.08,0.57,2.6;7:0.08,0.57,2.6',
      [{ x: 16.7, y: 2.5, w: 27, h: 3.9, at: 2.4, until: 4.2 }, { x: 1.6, y: 54.3, w: 15.2, h: 5.2, at: 5, c: 'amber' }]) +
    caption('Always know', 'Working, done or blocked', 'A spinner, a check or a pulse on every tab. The Activity pane lists them all.', true) });

  // 4. Resume (5s)
  scenes.push({ d: 5, html:
    cam([['api', '']], '0:0.5,0.5,1;5:0.5,0.5,1') +
    '<div class="card pop" data-at="0.8" style="left:470px;top:70px;width:520px;padding:18px 22px"><div class="mono">' +
    '<div><span class="b">api-refactor</span> $ claude <span class="y">--continue</span></div>' +
    '<div><span class="b">docs-site</span> $ codex <span class="y">resume --last</span></div>' +
    '<div><span class="b">infra</span> $ kiro-cli chat <span class="y">--resume</span></div>' +
    '<div><span class="b">web</span> $ opencode <span class="y">--continue</span></div>' +
    '<div class="g r" data-at="2" style="margin-top:8px">4 sessions restored</div></div></div>' +
    caption('Never lose your place', 'Restart. Reboot. Pick up where you were.', 'Sessions, terminals and open files come back. Each agent resumes with its own native flag.') });

  // 5. File Explorer and git always follow the active session (8s)
  scenes.push({ d: 8, html:
    cam([['api', '0-2.4'], ['xa', '2.4-5.2'], ['xd', '5.2-99']], '0:0.5,0.5,1;1:0.5,0.5,1;2:0.1,0.2,2.8;8:0.1,0.2,2.8',
      [{ x: 1.7, y: 14.0, w: 15, h: 4.0, at: 1.4, until: 2.4 }, { x: 1.7, y: 7.0, w: 15, h: 8.3, at: 3, until: 5 }, { x: 1.7, y: 7.1, w: 15, h: 6.7, at: 5.6 }]) +
    caption('Next to the session', 'Files and git follow the active tab', 'The Side Bar always shows the active session: its git status and its File Explorer, local, in WSL or on a remote host over SSH.', true) +
    '<div class="r" data-at="1.0" style="position:absolute;left:44px;bottom:44px;display:flex;gap:8px;z-index:7"><span class="chip" data-hl="0-5.2">api-refactor</span><span class="chip on2" data-hl="5.2-99">docs-site</span></div>' });

  // 6. Review Buddy (7s)
  scenes.push({ d: 7, html:
    cam([['rv', '']], '0:0.5,0.5,1;1.4:0.5,0.5,1;2.4:0.7,0.18,2.1;4.6:0.7,0.18,2.1;5.4:0.2,0.95,2.6;7:0.2,0.95,2.6',
      [{ x: 57.2, y: 5.9, w: 36.0, h: 27.2, at: 2.8, until: 5, c: 'amber' }, { x: 16.2, y: 94.4, w: 19, h: 4, at: 5.8 }]) +
    caption('Review Buddy', 'A second agent reviews the first', 'It reads the changes of a session and sends its findings back to the first agent, with nobody in between.', true, 5.4) });

  // 7. Goto Anything (5s)
  scenes.push({ d: 5, html:
    cam([['go', '']], '0:0.5,0.5,1;1.5:0.5,0.5,1;2.6:0.5,0.45,1.25;5:0.5,0.45,1.25') +
    caption('Find anything fast', 'Goto Anything', 'Menus, open pages and page actions in one search.') +
    keys(['Cmd', 'Shift', 'P'], 0.3, null) });

  // 8. Terminal (4s)
  scenes.push({ d: 4, html:
    cam([['web', '']], '0:0.5,0.5,1;1.2:0.5,0.5,1;3:0.45,0.2,2.2;4:0.45,0.2,2.2') +
    caption('Built for speed', 'A true terminal, drawn with OpenGL', 'Themes, mouse, clipboard and progress bars. Any interactive CLI works as in your shell.') });

  // 9. Outro (6s)
  scenes.push({ d: 6, html:
    '<div class="center">' +
    '<div class="big r" data-at="0.1" style="font-size:96px">Kennel</div>' +
    '<div class="tag r" data-at="0.4">Run every AI agent in one native window.</div>' +
    '<div class="row-chips r" data-at="0.9"><span class="chip on2">macOS</span><span class="chip on2">Linux</span><span class="chip on2">Windows</span></div>' +
    '<div class="r" data-at="1.4" style="font:600 22px var(--ui);color:#9fc3e8">github.com/eranif/kennel</div></div>' });

  var start = 0;
  scenes.forEach(function (s) {
    var el = document.createElement('div');
    el.className = 'scene';
    el.innerHTML = s.html;
    stage.appendChild(el);
    s.el = el; s.start = start; s.end = start + s.d; start += s.d;
    s.items = Array.prototype.slice.call(el.querySelectorAll('[data-at],[data-until],[data-hl],[data-type],[data-prog],[data-flip]'));
    s.cams = Array.prototype.slice.call(el.querySelectorAll('[data-cam]')).map(function (c) {
      return { el: c, k: c.getAttribute('data-cam').split(';').map(function (p) { var a = p.split(':'); var v = a[1].split(',').map(parseFloat); return { t: parseFloat(a[0]), cx: v[0], cy: v[1], s: v[2] }; }) };
    });
  });

  function range(v) { var p = v.split('-'); return [parseFloat(p[0]), parseFloat(p[1])]; }
  function ease(x) { return x < .5 ? 2 * x * x : 1 - Math.pow(-2 * x + 2, 2) / 2; }
  function camAt(k, rel) {
    if (rel <= k[0].t) { return k[0]; }
    for (var i = 1; i < k.length; i++) {
      if (rel <= k[i].t) { var a = k[i - 1], b = k[i], f = ease((rel - a.t) / (b.t - a.t)); return { cx: a.cx + (b.cx - a.cx) * f, cy: a.cy + (b.cy - a.cy) * f, s: a.s + (b.s - a.s) * f }; }
    }
    return k[k.length - 1];
  }

  function render(t) {
    scenes.forEach(function (s) {
      var on = t >= s.start && t < s.end;
      s.el.classList.toggle('on', on);
      if (!on) { return; }
      var rel = t - s.start;
      s.cams.forEach(function (c) {
        var p = camAt(c.k, rel);
        var tx = Math.min(0, Math.max(1280 - 1280 * p.s, 640 - p.cx * 1280 * p.s));
        var ty = Math.min(0, Math.max(722 - 722 * p.s, 361 - p.cy * 722 * p.s));
        c.el.style.transform = 'translate(' + tx + 'px,' + ty + 'px) scale(' + p.s + ')';
      });
      s.items.forEach(function (n) {
        var at = n.hasAttribute('data-at') ? parseFloat(n.getAttribute('data-at')) : null;
        var until = n.hasAttribute('data-until') ? parseFloat(n.getAttribute('data-until')) : null;
        if (at !== null) { n.classList.toggle('in', rel >= at); }
        if (until !== null) { n.classList.toggle('out', rel >= until); }
        if (n.hasAttribute('data-flip')) { n.classList.toggle('flip', rel >= parseFloat(n.getAttribute('data-flip'))); }
        if (n.hasAttribute('data-hl')) { var r = range(n.getAttribute('data-hl')); n.classList.toggle('hl', rel >= r[0] && rel < r[1]); }
        if (n.hasAttribute('data-type')) {
          var full = n.getAttribute('data-type'); var cps = parseFloat(n.getAttribute('data-cps')) || 30;
          var k = Math.max(0, Math.min(full.length, Math.floor((rel - at) * cps))); n.textContent = full.slice(0, k);
        }
        if (n.hasAttribute('data-prog')) { var g = range(n.getAttribute('data-prog')); n.style.width = (Math.max(0, Math.min(1, (rel - g[0]) / (g[1] - g[0]))) * 100) + '%'; }
      });
    });
  }

  var t = 0, playing = true, last = null;
  // #rec or #rec@<epoch ms>: for a screen recording (see media/AGENTS.md): no controls, the
  // picture fills the window, and the clock starts at the given time (default: in 1.5 seconds).
  // Before that the page shows the first frame, so a recording can start early.
  var mrec = /^#rec(?:@(\d+))?$/.exec(location.hash);
  if (mrec) {
    var rs = document.createElement('style');
    rs.textContent = 'body{padding:0;gap:0;background:#000}#ctl{display:none}#frame{max-width:none;width:min(100vw,177.78vh);border:0;border-radius:0}';
    document.head.appendChild(rs);
    playing = false;
    var startAt = mrec[1] ? parseFloat(mrec[1]) : Date.now() + 1500;
    setTimeout(function () { last = null; playing = true; }, Math.max(0, startAt - Date.now()));
  }
  // #t<seconds>: a frozen frame at that time, without transitions (to check the highlights).
  var mt = /^#t(\d+(?:\.\d+)?)$/.exec(location.hash);
  if (mt) {
    var ts = document.createElement('style');
    ts.textContent = '*{transition:none !important;animation:none !important}';
    document.head.appendChild(ts);
    playing = false; t = parseFloat(mt[1]);
  }
  var seek = document.getElementById('seek'), timeEl = document.getElementById('time'), playBtn = document.getElementById('play');
  function fmt(x) { var s = Math.floor(x); return Math.floor(s / 60) + ':' + ('0' + (s % 60)).slice(-2); }
  function paint() { render(t); seek.value = t; timeEl.textContent = fmt(t) + ' / ' + fmt(TOTAL); }
  function tick(now) {
    if (playing) {
      if (last !== null) { t += (now - last) / 1000; }
      if (t >= TOTAL) { t = TOTAL - 0.001; playing = false; playBtn.textContent = 'Play'; }
      paint();
    }
    last = now; requestAnimationFrame(tick);
  }
  playBtn.addEventListener('click', function () { if (t >= TOTAL - 0.01) { t = 0; } playing = !playing; playBtn.textContent = playing ? 'Pause' : 'Play'; });
  document.getElementById('restart').addEventListener('click', function () { t = 0; playing = true; playBtn.textContent = 'Pause'; paint(); });
  seek.addEventListener('input', function () { t = parseFloat(seek.value); paint(); });
  document.addEventListener('keydown', function (e) { if (e.code === 'Space' && e.target === document.body) { e.preventDefault(); playBtn.click(); } });

  var frame = document.getElementById('frame');
  function fit() { stage.style.transform = 'scale(' + (frame.clientWidth / 1280) + ')'; }
  window.addEventListener('resize', fit); fit();

  paint(); requestAnimationFrame(tick);
})();
</script>
</body>
</html>
'''
for key, name in [("api", "api-refactor"), ("docs", "docs-site"), ("infra", "infra"), ("web", "web"), ("switch", "switcher"), ("goto", "goto-anything"), ("explorer_api", "explorer-api"), ("explorer_docs", "explorer-docs"), ("review", "review-buddy"), ("scan", "first-scan")]:
    HTML = HTML.replace(f"@@{key}@@", b64(name))
open(f"{D}/kennel-promo.html", "w").write(HTML)
print(len(HTML) // 1024, "KB")
