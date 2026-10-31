#include "core/GitDriverLocal.h"

ProcessOutput GitDriverLocal::RunGit(const std::vector<std::string> &args) {
  std::vector<std::string> argv = {"git", "-C",
                                   GetFolder().ToStdString(wxConvUTF8)};
  argv.insert(argv.end(), args.begin(), args.end());
  return Process::RunProcessAndWait(argv);
}
