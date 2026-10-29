#pragma once

#include <wx/intl.h>
#include <wx/string.h>

// Where the files (and the programs) of a session live.
enum class HostType {
  kLocalHost, // This machine
  kRemote,    // Another machine, reached with ssh
  kWSL,       // A WSL distribution of this machine
};

// A place that has files: this machine, a remote host or a WSL distribution.
// The file views (FileTreeCtrl) are built from one, and the events about files
// (FileEvent) say which one the file is on.
struct Account {
  HostType m_type{HostType::kLocalHost};
  // Empty for localhost. The host name (or its ~/.ssh/config alias) for a
  // remote account, the name of the distribution (e.g. "Ubuntu-24.04") for WSL.
  wxString m_name;
  // A remote account: the ssh user. Empty means the ssh default.
  wxString m_user;
  // The folder a view of the account starts in. Empty means the default of the
  // kind of account: the home folder of this machine, the root of a WSL
  // distribution, the home folder of the remote user.
  wxString m_rootDir;

  static Account LocalHost(const wxString &rootDir = wxEmptyString) {
    Account account;
    account.m_rootDir = rootDir;
    return account;
  }
  static Account WSL(const wxString &distro,
                     const wxString &rootDir = wxEmptyString) {
    Account account;
    account.m_type = HostType::kWSL;
    account.m_name = distro;
    account.m_rootDir = rootDir;
    return account;
  }
  static Account Remote(const wxString &host,
                        const wxString &user = wxEmptyString,
                        const wxString &rootDir = wxEmptyString) {
    Account account;
    account.m_type = HostType::kRemote;
    account.m_name = host;
    account.m_user = user;
    account.m_rootDir = rootDir;
    return account;
  }

  bool IsLocalHost() const { return m_type == HostType::kLocalHost; }
  bool IsRemote() const { return m_type == HostType::kRemote; }
  bool IsWSL() const { return m_type == HostType::kWSL; }

  // The text that names the account to the user: "Local File System",
  // "WSL: Ubuntu-24.04", "user@host".
  wxString GetDisplayName() const {
    switch (m_type) {
    case HostType::kWSL:
      return wxString::Format(_("WSL: %s"), m_name);
    case HostType::kRemote:
      return m_user.empty() ? m_name : m_user + "@" + m_name;
    case HostType::kLocalHost:
      break;
    }
    return _("Local File System");
  }

  // Tells two accounts apart (the start folder does not count): "",
  // "wsl:<name>" or "ssh:<user>@<host>". Fit to be the key of a map.
  wxString GetId() const {
    switch (m_type) {
    case HostType::kWSL:
      return "wsl:" + m_name;
    case HostType::kRemote:
      return "ssh:" + m_user + "@" + m_name;
    case HostType::kLocalHost:
      break;
    }
    return wxEmptyString;
  }

  bool operator==(const Account &other) const {
    return GetId() == other.GetId() && m_rootDir == other.m_rootDir;
  }
};
