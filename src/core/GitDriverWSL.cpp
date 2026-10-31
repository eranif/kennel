#include "core/GitDriverWSL.h"

ProcessOutput GitDriverWSL::RunGit(const std::vector<std::string> &args) {
  std::vector<std::string> argv = {"wsl.exe",
                                   "-d",
                                   m_distro.ToStdString(wxConvUTF8),
                                   "--cd",
                                   GetFolder().ToStdString(wxConvUTF8),
                                   "--",
                                   "git"};
  argv.insert(argv.end(), args.begin(), args.end());
  return Process::RunProcessAndWait(argv);
}
