#pragma once

#include <wx/filename.h>
#include <wx/string.h>

// Resolves the application's base directory (~/.kennel) and all well-known
// paths beneath it. UI-agnostic (wxBase only, no GUI) so it stays
// unit-testable; the home directory can be injected for tests.
class AppPaths {
public:
  // Resolves the real user home portably (wxFileName::GetHomeDir) and roots
  // the app at <home>/.kennel.
  static AppPaths Default();

  // Constructs paths rooted at <home>/.kennel for an explicit home dir.
  static AppPaths WithHome(const wxString &home);

  wxFileName Root() const;          // <home>/.kennel
  wxFileName ConfigFile() const;    // root/config.json
  wxFileName WorkspaceFile() const; // root/workspace.json
  wxFileName HostsFile() const;     // root/hosts.json
  wxFileName PersistFile() const;   // root/.persist.json
  wxFileName SessionsDir() const;   // root/sessions
  wxFileName LogsDir() const;       // root/logs
  wxFileName AssetsDir() const;     // root/assets (icons)

  // Creates root, sessions/, and logs/ if missing. Idempotent. Returns true
  // on success; on failure the error message is written to *error (if given).
  bool EnsureDirectories(wxString *error = nullptr) const;

private:
  explicit AppPaths(const wxFileName &root);
  wxFileName m_root; // a directory-form wxFileName for <home>/.kennel
};
