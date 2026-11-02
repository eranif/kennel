#include "app/FileProvider.hpp"
#include "core/Logger.h"

#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/intl.h>
#include <wx/log.h>
#include <wx/utils.h>

//===------------------------
// Local Files Provider
//===------------------------
std::vector<FileProvider::File>
LocalFilesProvider::ListFiles(const wxString &dir) {
  std::vector<File> result;
  if (!wxDirExists(dir)) {
    return result;
  }

  wxDir d(dir);
  if (!d.IsOpened()) {
    return result;
  }

  wxString entry;
  const int flags = wxDIR_DIRS | (m_dirsOnly ? 0 : wxDIR_FILES);

  if (d.GetFirst(&entry, wxEmptyString, flags)) {
    do {
      wxString fullPath = dir + GetPathSeparator() + entry;
      NormalizePath(fullPath);
      const bool isDir = wxDirExists(fullPath);

      File f;
      f.path = fullPath;
      f.type = isDir ? FileType::kDir : FileType::kFile;
      f.size = 0;
      if (!isDir) {
        wxFileName fn(fullPath);
        if (fn.FileExists()) {
          f.size = static_cast<size_t>(fn.GetSize().ToULong());
        }
      }
      result.push_back(f);
    } while (d.GetNext(&entry));
  }

  return result;
}

Status LocalFilesProvider::MakeFolder(const wxString &path) {
  if (wxFileName::Exists(path, wxFILE_EXISTS_ANY | wxFILE_EXISTS_NO_FOLLOW)) {
    return Status::Error(wxString::Format(_("%s already exists"), path));
  }
  wxLogNull noLog; // The error is returned, not shown by wx
  if (!wxFileName::Mkdir(path, wxS_DIR_DEFAULT)) {
    return Status::Error(
        wxString::Format(_("Could not create the folder %s"), path));
  }
  return Status::Ok();
}

Status LocalFilesProvider::MakeFile(const wxString &path) {
  if (wxFileName::Exists(path, wxFILE_EXISTS_ANY | wxFILE_EXISTS_NO_FOLLOW)) {
    return Status::Error(wxString::Format(_("%s already exists"), path));
  }
  wxLogNull noLog; // The error is returned, not shown by wx
  wxFile file;
  if (!file.Create(path, /*overwrite=*/false)) {
    return Status::Error(
        wxString::Format(_("Could not create the file %s"), path));
  }
  return Status::Ok();
}

Status LocalFilesProvider::Remove(const wxString &path) {
  wxLogNull noLog; // The error is returned, not shown by wx
  // A symbolic link (also one to a folder) is a file here: only the link goes.
  const bool isLink =
      wxFileName::Exists(path, wxFILE_EXISTS_SYMLINK | wxFILE_EXISTS_NO_FOLLOW);
  bool removed = false;
  if (!isLink && wxDirExists(path)) {
    // Recursive: it does not follow the symbolic links inside (see
    // wxFileName::Rmdir()).
    removed = wxFileName::Rmdir(path, wxPATH_RMDIR_RECURSIVE);
  } else {
    removed = wxRemoveFile(path);
  }
  if (!removed) {
    return Status::Error(wxString::Format(_("Could not delete %s"), path));
  }
  return Status::Ok();
}

void LocalFilesProvider::ChangeDir(const wxString &path) {
  if (wxDirExists(path)) {
    m_dir = path;
  }
}

void LocalFilesProvider::Up() {
  wxFileName fn{m_dir, wxEmptyString};
  if (fn.GetDirCount() == 0) {
    return;
  }
  fn.RemoveLastDir();
  const wxString parent = fn.GetPath();
  if (!parent.empty() && wxDirExists(parent)) {
    m_dir = parent;
  }
}

//===------------------------
// WSL Files Provider
//===------------------------

std::vector<FileProvider::File>
WSLFilesProvider::ListFiles(const wxString &dir) {
  wxString modDir = dir;
  if (modDir.StartsWith("/")) {
    modDir.Remove(0, 1);
  } else if (modDir.StartsWith(m_uncPrefix)) {
    modDir.Remove(0, m_uncPrefix.size());
  }

  modDir.Replace("/", "\\");
  wxString fixedDir = wxString::Format("%s\\%s\\", m_uncPrefix, modDir);
  auto files = LocalFilesProvider::ListFiles(fixedDir);
  for (auto &f : files) {
    if (wxDirExists(f.path)) {
      f.type = FileType::kDir;
    } else {
      f.type = FileType::kFile;
    }
    f.path.Replace(m_uncPrefix, wxEmptyString);
    while (f.path.Replace("\\", "/")) {
    }
    while (f.path.Replace("//", "/")) {
    }
  }
  return files;
}

//===------------------------
// Remote Files Provider
//===------------------------

RemoteFilesProvider::RemoteFilesProvider(std::shared_ptr<KennelRemote> remote,
                                         const wxString &dir, bool dirsOnly)
    : FileProvider(dirsOnly), m_dir{dir}, m_remote{std::move(remote)} {
  m_name = m_remote->ConnectString();
}

std::vector<FileProvider::File>
RemoteFilesProvider::ListFiles(const wxString &dir) {
  const wxString directory = dir.empty() ? m_dir : dir;

  wxString cwd;
  auto entries = m_remote->ListFiles(directory, &cwd);
  if (!cwd.empty()) {
    m_dir = cwd;
  }

  std::vector<File> files;
  for (const auto &e : entries) {
    if (e.name.StartsWith(".")) {
      continue;
    }

    File f;
    f.path = m_dir + "/" + e.name;
    NormalizePath(f.path);

    f.type = e.isDir ? FileType::kDir : FileType::kFile;
    f.size = e.size;

    if (IsDirsOnly() && f.IsFile()) {
      continue;
    }
    files.push_back(f);
  }
  return files;
}

void RemoteFilesProvider::ChangeDir(const wxString &path) { m_dir = path; }

void RemoteFilesProvider::Up() {
  if (m_dir.empty()) {
    return;
  }
  wxString newDir = m_dir.BeforeLast('/');
  if (newDir.empty()) {
    newDir = "/";
  }
  m_dir = newDir;
}

std::unique_ptr<FileProvider> CreateFileProvider(const Account &account,
                                                 bool dirsOnly) {
  switch (account.m_type) {
  case HostType::kWSL:
    return std::make_unique<WSLFilesProvider>(account.m_name, account.m_rootDir,
                                              dirsOnly);
  case HostType::kRemote:
    return std::make_unique<RemoteFilesProvider>(
        std::make_shared<KennelRemote>(account.m_name, account.m_user),
        account.m_rootDir, dirsOnly);
  case HostType::kLocalHost:
    break;
  }
  return std::make_unique<LocalFilesProvider>(
      account.m_rootDir.empty() ? ::wxGetHomeDir() : account.m_rootDir,
      dirsOnly);
}
