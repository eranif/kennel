#include "core/AppPaths.h"

#include <wx/dir.h>

namespace {
// Builds a directory-form wxFileName for <home>/.kennel.
wxFileName MakeRoot(const wxString &home) {
  wxFileName root = wxFileName::DirName(home);
  root.AppendDir(".kennel");
  return root;
}
} // namespace

AppPaths::AppPaths(const wxFileName &root) : m_root(root) {}

AppPaths AppPaths::Default() {
  return AppPaths(MakeRoot(wxFileName::GetHomeDir()));
}

AppPaths AppPaths::WithHome(const wxString &home) {
  return AppPaths(MakeRoot(home));
}

wxFileName AppPaths::Root() const { return m_root; }

wxFileName AppPaths::ConfigFile() const {
  wxFileName f = m_root;
  f.SetFullName("config.json");
  return f;
}

wxFileName AppPaths::WorkspaceFile() const {
  wxFileName f = m_root;
  f.SetFullName("workspace.json");
  return f;
}

wxFileName AppPaths::HostsFile() const {
  wxFileName f = m_root;
  f.SetFullName("hosts.json");
  return f;
}

wxFileName AppPaths::PersistFile() const {
  wxFileName f = m_root;
  f.SetFullName(".persist.json");
  return f;
}

wxFileName AppPaths::SessionsDir() const {
  wxFileName d = m_root;
  d.AppendDir("sessions");
  return d;
}

wxFileName AppPaths::LogsDir() const {
  wxFileName d = m_root;
  d.AppendDir("logs");
  return d;
}

wxFileName AppPaths::AssetsDir() const {
  wxFileName d = m_root;
  d.AppendDir("assets");
  return d;
}

bool AppPaths::EnsureDirectories(wxString *error) const {
  // Note: sessions/ is intentionally not created here. Sessions resume via the
  // client's own --resume/--continue, so the app writes no per-session files.
  const wxFileName dirs[] = {m_root, LogsDir(), AssetsDir()};
  for (const wxFileName &d : dirs) {
    if (d.DirExists()) {
      continue;
    }
    if (!d.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
      if (error) {
        *error =
            wxString::Format("Failed to create directory: %s", d.GetPath());
      }
      return false;
    }
  }
  return true;
}
