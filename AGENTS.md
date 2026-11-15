# Rules for agents working on Kennel

See `CLAUDE.md` for the build, layout and coding rules. This file is about how
to work with the user.

- **Ask, do not assume.** If the request is ambiguous, or you are about to
  choose something that the user did not specify (a class, a library, an
  approach, where a feature lives), stop and ask one short question first.
- **Do what was named.** If the user names a class or an approach (for example
  `wxAuiNotebook`), use that one. Do not substitute another, and do not add
  extra changes to other files or classes that were not asked for.
- **Do not state what you have not read.** Open the code before saying what a
  class is or does.
- **Find every user of what you change.** When a change breaks something the
  code relied on (an object that always had a terminal now may not, a call is now
  asynchronous, a new state), grep the whole repo for its users and read them, and
  trace the startup path (restoring sessions) before saying it is done. See
  `CLAUDE.md`, "Build & verify".
- **NEVER name a function `XxxOf(x)` or `XxxFor(x)`** (`DistroOf`, `TmuxTargetOf`,
  `DefaultIconFor`). Say what it returns with a verb: `GetDistroNameFrom(shell)`,
  `IsXxx()`, `GetXxx()`, `FindXxx()`. `For` only when it is absolutely needed (a wxWidgets
  API). The old ones are debt: do not copy them. See `CLAUDE.md`, "Hard rules".
- **A method on the object, NOT a global helper.** If it is about one of our types
  (`Session`, `Account`, `AgentDef`, a page), it is a non-static method of that type:
  `session.IsTmuxSession()`, `agent.GetDefaultIcon()`, not `DefaultIconFor(agent)` and not
  a `static` somewhere else. A `static` method is the last resort.
- **Do NOT use an anonymous namespace (or a file-level `static`) for helpers** unless you
  have no choice. Use a private non-static method, a method of the object, or a small
  class. If you are not sure that you have no choice, you have one. The existing ones are
  debt: do not add to them. See `CLAUDE.md`, "Hard rules".
- **Just change the code.** No throwaway test programs or extra verification:
  make the change, build, and report what is not verified.
- **Do not commit or push** until the user says so.
