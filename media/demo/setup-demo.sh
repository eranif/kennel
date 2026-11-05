#!/bin/bash
# Creates the demo environment for the promo screenshots (see media/AGENTS.md):
#   $DEMO_ROOT/home        a fresh HOME for Kennel (config, workspace, fake agents)
#   $DEMO_ROOT/home/bin    fake agent scripts (named like the real tools)
#   $DEMO_PROJECTS         four small git repositories (kept at a short path:
#                          the status line of Kennel shows it)
# Nothing here touches your real ~/.kennel.
set -euo pipefail

DEMO_ROOT=${DEMO_ROOT:-/tmp/kennel-demo}
DEMO_PROJECTS=${DEMO_PROJECTS:-/tmp/k}
HOME_DIR=$DEMO_ROOT/home

rm -rf "$DEMO_ROOT" "$DEMO_PROJECTS"
mkdir -p "$HOME_DIR/.kennel" "$HOME_DIR/bin" \
  "$DEMO_PROJECTS"/{api/src/routes,api/src/services,docs/docs,web/src,infra/modules}

repo() { # repo <dir>: a git repository with one commit
  (cd "$1" && git init -q . && git config user.email demo@example.com &&
    git config user.name Demo)
}

# --- api: handlers in one file (the agent splits them), committed
repo "$DEMO_PROJECTS/api"
(cd "$DEMO_PROJECTS/api" &&
  cat > src/routes/users.ts <<'T'
const users = [{ id: 1, name: 'Ada' }, { id: 2, name: 'Linus' }];

export function listUsers(req, res) { res.json(users); }

export function getUser(req, res) {
  const user = users.find((u) => u.id === Number(req.params.id));
  if (user) { res.json(user); } else { res.status(404).end(); }
}

export function createUser(req, res) {
  const user = { id: users.length + 1, name: req.body.name };
  users.push(user);
  res.status(201).json(user);
}
T
  printf 'export const orders = [];\n' > src/routes/orders.ts &&
  printf '# API\n' > README.md && printf '{}\n' > package.json &&
  git add -A && git commit -q -m init)
# --- docs
repo "$DEMO_PROJECTS/docs"
(cd "$DEMO_PROJECTS/docs" && printf '# Docs\n' > README.md &&
  printf '# Install\n' > docs/install.md && printf '# Usage\n' > docs/usage.md &&
  git add -A && git commit -q -m init && echo more >> docs/install.md &&
  echo x > scratch.txt)
# --- web
repo "$DEMO_PROJECTS/web"
(cd "$DEMO_PROJECTS/web" && printf '# Web\n' > README.md &&
  printf 'export const App = 1;\n' > src/App.tsx &&
  printf 'export const Header = 1;\n' > src/Header.tsx &&
  git add -A && git commit -q -m init && echo x >> src/Header.tsx &&
  echo 'export const ThemeToggle = 1;' > src/ThemeToggle.tsx)
# --- infra
repo "$DEMO_PROJECTS/infra"
(cd "$DEMO_PROJECTS/infra" && printf 'resource {}\n' > main.tf &&
  printf 'resource {}\n' > modules/net.tf && git add -A && git commit -q -m init &&
  echo change >> main.tf)

# --- fake agents, config and workspace
python3 - "$HOME_DIR" "$DEMO_PROJECTS" <<'PY'
import json, os, shutil, sys
home, proj = sys.argv[1:3]
E = '\\033'
B = f'{E}[1m'; R = f'{E}[0m'
D = f'{E}[38;5;249m'   # light gray: the "bright black" of a theme is too dark on Mariana
G = f'{E}[38;5;114m'; Y = f'{E}[38;5;215m'; BL = f'{E}[38;5;110m'; OR = f'{E}[38;5;173m'
BG = f'{E}[48;5;239m'; W = f'{E}[38;5;255m'
LINE = '─' * 118

def write(name, body):
    path = f'{home}/bin/{name}'
    open(path, 'w').write(f"#!/bin/bash\nprintf '\\033[2J\\033[H'\n{body}\nexec sleep 100000\n")
    os.chmod(path, 0o755)

# OSC 7501 is the Program Status Protocol: it drives the spinner, the check and the pulse.
write('claude', f"""printf ' {OR}▐▛███▜▌{R}   {B}Claude Code{R} {D}v2.1.201{R}\\n'
printf '{OR}▝▜█████▛▘{R}  {D}Haiku 4.5 · API Usage Billing{R}\\n'
printf '  {OR}▘▘ ▝▝{R}    {D}{proj}/api{R}\\n\\n'
printf '{BG}{W}> split the handlers into routes and services{R}{BG}                                                         {R}\\n\\n'
printf '{W}●{R} I will move the request handlers into {B}routes/{R} and the logic into {B}services/{R}.\\n'
printf '  {D}⎿  Updated src/routes/users.ts (+2 -0){R}\\n  {D}⎿  Wrote src/services/users.ts (+1){R}\\n\\n'
printf '{W}●{R} Bash(npm test)\\n  {D}⎿  142 passed, 0 failed{R}\\n\\n'
printf '{W}●{R} The refactor is done: all 142 tests pass.\\n\\n'
printf '{D}✻ Baked for 14s{R}\\n\\n'
printf '{D}{LINE}{R}\\n> \\n{D}{LINE}{R}\\n  {D}? for shortcuts{R}'
printf '\\033[2A\\033[3G'
printf '\\033]7501;state=done\\007'""")
write('codex', f"""printf '{D}╭──────────────────────────────────────────────╮{R}\\n{D}│{R} {B}>_ OpenAI Codex{R} {D}(v0.4){R}                         {D}│{R}\\n{D}│{R}                                              {D}│{R}\\n{D}│{R} {D}model:{R}     gpt-5-codex                       {D}│{R}\\n{D}│{R} {D}directory:{R} {proj}/docs                      {D}│{R}\\n{D}╰──────────────────────────────────────────────╯{R}\\n\\n'
printf '{BG}{W}> rewrite the install guide{R}{BG}                                                          {R}\\n\\n'
printf '{W}●{R} Reading docs/install.md and docs/usage.md\\n  {D}⎿  Edit(docs/install.md){R}\\n\\n{Y}⠙ Working{R} {D}(12s · esc to interrupt){R}\\n\\n'
printf '{D}{LINE}{R}\\n> \\n{D}{LINE}{R}'
printf '\\033[1A\\033[3G'
printf '\\033]7501;state=working:progress=40\\007'""")
write('opencode', f"""printf '{B}opencode{R} {D}{proj}/web{R}\\n\\n'
printf '{BG}{W}> add a dark mode toggle to the header{R}{BG}                                                     {R}\\n\\n{W}●{R} Done. Added ThemeToggle.tsx and wired it into Header.tsx.\\n\\n'
printf '{D}{LINE}{R}\\n> \\n{D}{LINE}{R}'
printf '\\033[1A\\033[3G'
printf '\\033]7501;state=idle\\007'""")
# kiro-cli is the blocked session (infra) and also the reviewer of Review Buddy.
# The reviewer is started with a long prompt; the resume flags are short.
open(f'{home}/bin/kiro-cli', 'w').write(f"""#!/bin/bash
printf '\\033[2J\\033[H'
REVIEW=0; for a in "$@"; do [ ${{#a}} -gt 40 ] && REVIEW=1; done
if [ $REVIEW = 1 ]; then
printf '{B}Review Buddy{R} {D}· reviewing the changes of api-refactor{R}\\n\\n'
printf '{W}●{R} Reading the diff: 6 files changed\\n  {D}⎿  src/routes/users.ts  src/services/users.ts  src/routes/orders.ts{R}\\n\\n'
printf '{W}●{R} Findings\\n'
printf '  {Y}1.{R} {B}src/routes/users.ts:41{R}      Missing null check on req.user\\n'
printf '  {Y}2.{R} {B}src/services/users.ts:17{R}   Unhandled promise rejection\\n'
printf '  {Y}3.{R} {B}src/routes/orders.ts:88{R}    SQL built from user input\\n\\n'
printf '{G}→ Sending 3 findings to the Claude session{R}\\n'
printf '\\033]7501;state=working:progress=70\\007'
else
printf '{B}Kiro CLI{R}\\n{D}{proj}/infra{R}\\n\\n'
printf '{BG}{W}> apply the terraform plan{R}{BG}                                                              {R}\\n\\n{W}●{R} The plan adds 3 resources and changes 1.\\n\\n'
printf '{Y}Allow terraform apply? [y/n/t]{R} '
printf '\\033]7501;state=blocked\\007'
fi
exec sleep 100000
""")
os.chmod(f'{home}/bin/kiro-cli', 0o755)


# --- DEMO_REAL=1: the real claude and opencode (kiro-cli stays a script: the blocked session and the reviewer). The wrappers give them your real HOME
# (their login), a cheap model and a first prompt that depends on the project. Codex is not used.
if os.environ.get('DEMO_REAL'):
    real_home = os.path.expanduser('~')
    def wrapper(name, body):
        path = f'{home}/bin/{name}'
        open(path, 'w').write(f'#!/bin/bash\nexport HOME={real_home}\n{body}\n')
        os.chmod(path, 0o755)
    wrapper('claude', f"""unset CLAUDECODE CLAUDE_CODE_CHILD_SESSION CLAUDE_CODE_ENTRYPOINT
git -C "$PWD" stash -u -q 2>/dev/null; git -C "$PWD" stash drop -q 2>/dev/null
case "$PWD" in
  */docs) P='Do not ask questions. The product is Acme Sync, a command line tool that syncs a folder to the cloud. Write ten guides, docs/guide-1.md to docs/guide-10.md, one file at a time, each about 40 lines (install, login, first sync, ignore rules, conflicts, scheduling, proxies, logs, upgrades, uninstall). Do not run shell commands.' ;;
  *) P='split the handlers of src/routes/users.ts into routes and services. Do not run anything else. Reply with one short sentence when done.' ;;
esac
exec {shutil.which('claude')} --model haiku --permission-mode acceptEdits "$P" """)
    wrapper('opencode', f"""git -C "$PWD" stash -u -q 2>/dev/null; git -C "$PWD" stash drop -q 2>/dev/null
exec {shutil.which('opencode')} --prompt 'add a dark mode toggle to the header by editing src/Header.tsx. Do not run shell commands. Reply with one short sentence when done.' """)

agents = [{"name": n, "executable": f"{home}/bin/{n}", "baseArgs": [], "env": {}, "extraArgs": [],
           "iconPath": "", "loginShell": "/bin/bash --login -i", "remoteHost": "", "remoteUser": "",
           "resumeArg": "", "nonInteractiveArg": ""} for n in ("claude", "kiro-cli", "opencode")]
json.dump({"agents": agents, "global": None, "version": 1}, open(f'{home}/.kennel/config.json', 'w'), indent=1)

def sess(name, agent, d, group="Default"):
    return {"name": name, "agentName": agent, "groupName": group, "workingDir": d,
            "loginShell": "/bin/bash --login -i", "plainTerminal": False}
sessions = [sess("api-refactor", "claude", f"{proj}/api"), sess("docs-site", "claude", f"{proj}/docs"),
            sess("infra", "kiro-cli", f"{proj}/infra", "Servers"), sess("web", "opencode", f"{proj}/web")]
refs = [{"groupName": s["groupName"], "name": s["name"]} for s in sessions]
json.dump({"version": 1, "workspace": {"sessions": sessions, "files": [], "archived": [],
                                       "recent": refs, "tabOrder": refs}},
          open(f'{home}/.kennel/workspace.json', 'w'))
# A second HOME with no agents and no workspace: the first start scans for agents.
os.makedirs(f'{home}/../home-fresh/.kennel', exist_ok=True)
json.dump({"agents": [], "global": None, "version": 1}, open(f'{home}/../home-fresh/.kennel/config.json', 'w'))
PY
echo "Demo ready: HOME=$HOME_DIR  projects=$DEMO_PROJECTS"
