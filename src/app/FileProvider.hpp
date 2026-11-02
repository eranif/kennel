#pragma once

#include "core/Account.h"
#include "core/KennelRemote.h"

#include <wx/filefn.h>
#include <wx/string.h>
#include <wx/utils.h>

#include <memory>
#include <vector>

struct FileProvider {
  bool m_dirsOnly{false};
  wxString m_name;

  enum class FileType {
    kDir,
    kFile,
  };

  struct File {
    wxString path;
    size_t size{0};
    FileType type{FileType::kFile};

    inline bool IsFile() const { return type == FileType::kFile; }
    inline bool IsFolder() const { return type == FileType::kDir; }
  };

  FileProvider(bool dirsOnly) : m_dirsOnly{dirsOnly} {}
  virtual ~FileProvider() = default;

  // Gets ready to list: a remote host is connected here (it can take seconds).
  // The error when that failed. The others need nothing. It shows nothing, so
  // it can run on a worker thread (and so can ListFiles() of a remote host).
  virtual Status Connect() { return Status::Ok(); }
  virtual std::vector<File> ListFiles(const wxString &dir) = 0;
  // Make the folder or the empty file `path` (in the notation of ListFiles();
  // the parent must exist). A path that exists is an error.
  virtual Status MakeFolder(const wxString &path) = 0;
  virtual Status MakeFile(const wxString &path) = 0;
  // Deletes the file `path`, or the folder `path` and all in it. A symbolic
  // link is removed, never followed.
  virtual Status Remove(const wxString &path) = 0;
  virtual wxString GetCurrentWorkingDir() const = 0;
  virtual void ChangeDir(const wxString &path) = 0;
  virtual wxString GetHome() const = 0;
  virtual void Up() = 0;
  virtual bool HasHome() const = 0;
  inline bool IsDirsOnly() const { return m_dirsOnly; }
  virtual wxChar GetPathSeparator() const = 0;
  const wxString &GetName() const { return m_name; }

  virtual void NormalizePath(wxString &path) {
    path.Replace("\\", "/");
    path.Replace("//", "/");
  }
};

struct LocalFilesProvider : public FileProvider {
  wxString m_dir;

  ~LocalFilesProvider() override = default;
  LocalFilesProvider(const wxString &dir = wxEmptyString, bool dirsOnly = false)
      : FileProvider{dirsOnly} {
    m_name = _("Local File System");
    if (dir.empty() || !wxDirExists(dir)) {
      m_dir = ::wxGetCwd();
    } else {
      m_dir = dir;
    }
  }

  std::vector<File> ListFiles(const wxString &dir) override;
  Status MakeFolder(const wxString &path) override;
  Status MakeFile(const wxString &path) override;
  Status Remove(const wxString &path) override;
  void ChangeDir(const wxString &path) override;
  wxString GetCurrentWorkingDir() const override {
    wxString dir{m_dir};
    dir.Replace("\\", "/");
    dir.Replace("//", "/"); // remove any double '/'
    return dir;
  }

  bool HasHome() const override { return true; }
  wxString GetHome() const override { return ::wxGetHomeDir(); }
  void Up() override;
  wxChar GetPathSeparator() const override { return '/'; }
};

struct WSLFilesProvider : public LocalFilesProvider {
  wxString m_uncPrefix;

  ~WSLFilesProvider() override = default;
  WSLFilesProvider(const wxString &distroName,
                   const wxString &dir = wxEmptyString, bool dirsOnly = false)
      : LocalFilesProvider{dir, dirsOnly} {
    m_name = wxString::Format(_("WSL: %s"), distroName);
    m_uncPrefix = wxString::Format(R"(\\wsl$\%s)", distroName);
    if (dir.empty() || !wxDirExists(dir)) {
      m_dir = m_uncPrefix;
    } else {
      m_dir = dir;
    }
  }

  std::vector<File> ListFiles(const wxString &dir) override;
  // The paths are the Linux paths: the files are changed through the share of
  // the distribution (\\wsl$\<distro>).
  Status MakeFolder(const wxString &path) override {
    return LocalFilesProvider::MakeFolder(ToSharePath(path));
  }
  Status MakeFile(const wxString &path) override {
    return LocalFilesProvider::MakeFile(ToSharePath(path));
  }
  Status Remove(const wxString &path) override {
    return LocalFilesProvider::Remove(ToSharePath(path));
  }
  wxString GetCurrentWorkingDir() const override { return m_dir; }
  bool HasHome() const override { return true; }
  wxString GetHome() const override { return "/home"; }
  wxChar GetPathSeparator() const override { return '/'; }
  void Up() override {
    if (m_dir.empty()) {
      return;
    }
    wxString dir = m_dir;
    dir.Replace("\\", "/");
    wxString newDir = dir.BeforeLast('/');
    if (newDir.empty()) {
      newDir = "/";
    }
    m_dir = newDir;
  }

  void NormalizePath(wxString &) override {}

private:
  // The Linux path `path` as a path of the share.
  wxString ToSharePath(const wxString &path) const {
    wxString share = path;
    share.Replace("/", "\\");
    return m_uncPrefix + (share.StartsWith("\\") ? "" : "\\") + share;
  }
};

struct RemoteFilesProvider : public FileProvider {
  wxString m_dir;

  ~RemoteFilesProvider() override = default;
  RemoteFilesProvider(std::shared_ptr<KennelRemote> remote,
                      const wxString &dir = wxEmptyString,
                      bool dirsOnly = false);
  Status Connect() override { return m_remote->Start(); }
  std::vector<File> ListFiles(const wxString &dir) override;
  Status MakeFolder(const wxString &path) override {
    return m_remote->MakeFolder(path);
  }
  Status MakeFile(const wxString &path) override {
    return m_remote->MakeFile(path);
  }
  Status Remove(const wxString &path) override {
    return m_remote->Remove(path);
  }
  void ChangeDir(const wxString &path) override;
  wxString GetCurrentWorkingDir() const override { return m_dir; }
  bool HasHome() const override { return false; }
  wxString GetHome() const override { return wxEmptyString; }
  void Up() override;
  wxChar GetPathSeparator() const override { return '/'; }

private:
  std::shared_ptr<KennelRemote> m_remote;
};

// The provider that lists the files of `account`: the local file system, the
// file system of a WSL distro, or a remote host (over ssh). It starts in
// `account.m_rootDir`, or in the default folder of the kind of account.
std::unique_ptr<FileProvider> CreateFileProvider(const Account &account,
                                                 bool dirsOnly = false);
