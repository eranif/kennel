#pragma once

#include <wx/filename.h>
#include <wx/string.h>

#include "core/Status.h"

#include <vector>

struct SshHostEntry {
  wxString name;    // alias from the Host directive
  wxString address; // HostName value (IP or FQDN)
};

// Parses ~/.ssh/config (or any OpenSSH client config) and extracts the list of
// named host entries that have an explicit HostName directive. Wildcard entries
// (Host *) and entries without a HostName are skipped.
class SshConfigParser {
public:
  // Uses the standard OpenSSH config path: ~/.ssh/config.
  static SshConfigParser Default();

  // Uses an explicit config file path (useful for testing).
  explicit SshConfigParser(const wxFileName &path);

  // Parses the config file and returns the host entries found.
  // Returns an error status on I/O failure; an absent file yields an empty
  // list (OK) rather than an error.
  StatusOr<std::vector<SshHostEntry>> Parse() const;

private:
  wxFileName m_path;
};
