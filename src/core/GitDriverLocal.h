#pragma once

#include "core/GitDriver.h"

// Git on this machine.
class GitDriverLocal : public GitDriver {
public:
  explicit GitDriverLocal(const wxString &folder) : GitDriver(folder) {}
  ~GitDriverLocal() override { Shutdown(); }

protected:
  ProcessOutput RunGit(const std::vector<std::string> &args) override;
  wxPathFormat GetPathFormat() const override { return wxPATH_NATIVE; }
};
