#pragma once

#include "core/GitDriver.h"

// Git on a remote host, over ssh. `folder` is a path on the host. The login
// needs a key: ssh never asks for a password here.
class GitDriverSSH : public GitDriver {
public:
  // `user` empty: the ssh default.
  GitDriverSSH(const wxString &host, const wxString &user,
               const wxString &folder)
      : GitDriver(folder), m_host(host), m_user(user) {}
  ~GitDriverSSH() override { Shutdown(); }

protected:
  ProcessOutput RunGit(const std::vector<std::string> &args) override;

private:
  wxString m_host;
  wxString m_user;
};
