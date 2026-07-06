#pragma once

#include "UI.hpp"
#include "core/KennelRemote.h"

#include <wx/longlong.h>

#include <memory>
#include <optional>

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

  virtual std::vector<File> ListFiles(const wxString &dir) = 0;
  virtual wxString GetCurrentWorkingDir() const = 0;
  virtual void ChangeDir(const wxString &path) = 0;
  virtual wxString GetHome() const = 0;
  virtual void Up() = 0;
  virtual bool HasHome() const = 0;
  inline bool IsDirsOnly() const { return m_dirsOnly; }
  virtual wxChar GetPathSeparator() const = 0;
  const wxString &GetName() const { return m_name; }
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

  std::vector<File> ListFiles(const wxString &dir) override {
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
      f.path.Replace(m_uncPrefix, wxEmptyString);
      while (f.path.Replace("\\", "/")) {
      }
      while (f.path.Replace("//", "/")) {
      }
    }
    return files;
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
};

struct RemoteFilesProvider : public FileProvider {
  wxString m_dir;

  ~RemoteFilesProvider() override = default;
  RemoteFilesProvider(std::shared_ptr<KennelRemote> remote,
                      const wxString &dir = wxEmptyString,
                      bool dirsOnly = false);
  std::vector<File> ListFiles(const wxString &dir) override;
  void ChangeDir(const wxString &path) override;
  wxString GetCurrentWorkingDir() const override { return m_dir; }
  bool HasHome() const override { return false; }
  wxString GetHome() const override { return wxEmptyString; }
  void Up() override;
  wxChar GetPathSeparator() const override { return '/'; }

private:
  std::shared_ptr<KennelRemote> m_remote;
};

class FileBrowserDlg : public FileBrowserDlgBase {
public:
  FileBrowserDlg(wxWindow *parent, std::unique_ptr<FileProvider> provider);
  ~FileBrowserDlg() override;

  wxString GetPath() const {
    return m_selection.value_or(FileProvider::File{}).path;
  }

  void SetFilterFunction(
      std::function<bool(const FileProvider::File &)> filterCallback) {
    m_filterCallback = std::move(filterCallback);
  }

protected:
  void OnSelectionChanged(wxDataViewEvent &event) override;
  void OnSelect(wxCommandEvent &event) override;
  void OnSelectUI(wxUpdateUIEvent &event) override;
  void OnKeyDown(wxKeyEvent &event) override;
  void OnActivated(wxDataViewEvent &event) override;
  void PopulateView(const wxString &dir);
  void OnUp(wxCommandEvent &event);
  void OnHome(wxCommandEvent &event);
  void UpdateSelection(const wxDataViewItem &item);
  // Selects and scrolls to the first row whose name starts with `prefix`
  // (case-insensitive). No-op if there is no match.
  void JumpToTypeAhead(const wxString &prefix);

private:
  std::unique_ptr<FileProvider> m_provider;
  std::unordered_map<wxString, FileProvider::File> m_files;
  // Display order of the names shown in the view (m_files is unordered).
  std::vector<wxString> m_displayNames;
  std::optional<FileProvider::File> m_selection;

  // Type-ahead search state.
  wxString m_typeAhead;
  wxLongLong m_lastKeyTime{0};
  std::function<bool(const FileProvider::File &)> m_filterCallback{nullptr};
};
