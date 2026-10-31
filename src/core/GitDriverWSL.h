#pragma once

#include "core/GitDriver.h"

// Git in a WSL distribution. `folder` is a Linux path.
class GitDriverWSL : public GitDriver {
public:
  GitDriverWSL(const wxString &distro, const wxString &folder)
      : GitDriver(folder), m_distro(distro) {}
  ~GitDriverWSL() override { Shutdown(); }

protected:
  ProcessOutput RunGit(const std::vector<std::string> &args) override;

private:
  wxString m_distro;
};
