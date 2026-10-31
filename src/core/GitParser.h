#pragma once

#include "core/GitTypes.h"

#include <string>

// Turns the output of git into data. Pure functions, no process is started
// here. Unknown or broken lines are skipped, never an error.
namespace git {

// `git status --porcelain=v2 -b -z --untracked-files=all`
GitStatus ParseStatus(const std::string &output);

} // namespace git
