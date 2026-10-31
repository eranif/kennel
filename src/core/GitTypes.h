#pragma once

#include <wx/string.h>

#include <vector>

// Plain data about a git repository. GUI-free.

// One entry of `git status`: a file with a change.
struct GitChange {
  wxString path;     // Relative to the root of the repository
  wxString origPath; // A rename or a copy: where it came from
  // The letters of `git status --porcelain`: X is the index (staged), Y is the
  // work tree. '.' means no change on that side.
  char index{'.'};
  char worktree{'.'};
  bool untracked{false};
  bool unmerged{false};

  bool IsStaged() const { return !untracked && !unmerged && index != '.'; }
  bool IsUnstaged() const { return !untracked && !unmerged && worktree != '.'; }
};

struct GitStatus {
  wxString
      root; // The root folder of the repository (the paths are relative to it)
  wxString branch;   // Empty when HEAD is detached
  wxString oid;      // Empty before the first commit
  wxString upstream; // E.g. "origin/main"; empty if none
  int ahead{0};
  int behind{0};
  bool detached{false};
  std::vector<GitChange> changes;
};
