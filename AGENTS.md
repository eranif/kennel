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
- **Just change the code.** No throwaway test programs or extra verification:
  make the change, build, and report what is not verified.
- **Do not commit or push** until the user says so.
