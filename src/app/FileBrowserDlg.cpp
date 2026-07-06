#include "FileBrowserDlg.hpp"
#include "core/AppManager.h"
#include "core/Helpers.h"
#include <wx/msgdlg.h>

#include "app/AppUIGlobals.hpp"
#include <algorithm>
#include <wx/artprov.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/tokenzr.h>

#ifdef __WXMSW__
#define wxPATH_SEPARATOR '\\'
#else
#define wxPATH_SEPARATOR '/'
#endif

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
      const wxString fullPath = dir + wxFileName::GetPathSeparator() + entry;
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

void LocalFilesProvider::ChangeDir(const wxString &path) {
  if (wxDirExists(path)) {
    m_dir = path;
  }
}

void LocalFilesProvider::Up() {
  wxFileName fn{m_dir, wxEmptyString};
  if (fn.GetDirCount() == 1) {
    return;
  }
  fn.RemoveLastDir();
  const wxString parent = fn.GetPath();
  if (!parent.empty() && wxDirExists(parent)) {
    m_dir = parent;
  }
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

  wxBusyCursor bc{};
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

//===--------------------------------
// FileBrowserDlg
//===--------------------------------
FileBrowserDlg::FileBrowserDlg(wxWindow *parent,
                               std::unique_ptr<FileProvider> provider)
    : FileBrowserDlgBase(parent), m_provider{std::move(provider)} {
  wxString label;
  if (m_provider->IsDirsOnly()) {
    label << _("Choose a directory");
  } else {
    label << _("Choose a file");
  }
  label << wxString::Format(" (%s)", m_provider->GetName());
  SetLabel(label);

  if (m_provider->IsDirsOnly()) {
    // Hide the preview
    m_panelPreview->Hide();
  }

  auto &bmps = AppManager::Get().GetBitmaps();
  m_auibar->AddTool(wxID_UP, _("Up"), bmps.GetByAlias("up", false));
  m_auibar->AddTool(wxID_HOME, _("Home"), bmps.GetByAlias("home", false));
  m_auibar->Realize();

  Bind(wxEVT_TOOL, &FileBrowserDlg::OnUp, this, wxID_UP);
  Bind(wxEVT_TOOL, &FileBrowserDlg::OnHome, this, wxID_HOME);

  CallAfter(&FileBrowserDlg::PopulateView, m_provider->GetCurrentWorkingDir());
  CenterOnParent();
  GetSizer()->Layout();
}

FileBrowserDlg::~FileBrowserDlg() {}

void FileBrowserDlg::PopulateView(const wxString &dir) {
  m_textCtrlCurrnentDir->SetValue(dir);
  auto files = m_provider->ListFiles(dir);

  std::sort(files.begin(), files.end(),
            [](const FileProvider::File &a, const FileProvider::File &b) {
              const bool aDir = a.type == FileProvider::FileType::kDir;
              const bool bDir = b.type == FileProvider::FileType::kDir;
              if (aDir != bDir) {
                return aDir; // dirs first
              }
              return a.path.CmpNoCase(b.path) < 0;
            });

  auto &bmps = AppManager::Get().GetBitmaps();
  const wxBitmapBundle &dirIcon = bmps.GetByAlias("folder", false);
  const wxBitmapBundle &fileIcon = bmps.GetByAlias("file", false);
  const wxBitmapBundle &svgIcon = bmps.GetByAlias("file-svg", false);

  m_dvListCtrl->DeleteAllItems();
  m_files.clear();
  m_displayNames.clear();
  m_typeAhead.clear(); // reset any in-progress search when the folder changes

  for (const auto &f : files) {
    // Apply the filter before updating the UI
    if (m_filterCallback && !m_filterCallback(f)) {
      continue;
    }

    wxString name = f.path.AfterLast(m_provider->GetPathSeparator());
    const wxBitmapBundle *img{&dirIcon};
    if (f.IsFile() && wxFileName(f.path).GetExt().Lower() == "svg") {
      img = &svgIcon;
    } else if (f.IsFile()) {
      img = &fileIcon;
    }

    wxDataViewIconText iconText(name, *img);
    wxVariant col0;
    col0 << iconText;

    wxString sizeStr;
    if (f.IsFile()) {
      if (f.size < 1024) {
        sizeStr = wxString::Format("%zu B", f.size);
      } else if (f.size < 1024 * 1024) {
        sizeStr =
            wxString::Format("%.1f KB", static_cast<double>(f.size) / 1024.0);
      } else {
        sizeStr = wxString::Format("%.1f MB", static_cast<double>(f.size) /
                                                  (1024.0 * 1024.0));
      }
    } else {
      sizeStr = "0";
    }

    wxVector<wxVariant> cols;
    cols.push_back(col0);
    cols.push_back(wxVariant(sizeStr));
    m_dvListCtrl->AppendItem(cols);
    m_files.insert({name, f});
    m_displayNames.push_back(name);
  }

  if (m_dvListCtrl->GetItemCount()) {
    m_dvListCtrl->SelectRow(0);
    CallAfter(&FileBrowserDlg::UpdateSelection, m_dvListCtrl->RowToItem(0));
  }
  m_dvListCtrl->CallAfter(&wxDataViewListCtrl::SetFocus);
}

void FileBrowserDlg::OnActivated(wxDataViewEvent &event) {
  UpdateSelection(event.GetItem());
  if (!m_selection) {
    return;
  }
  if (m_selection->type == FileProvider::FileType::kDir) {
    if (::wxGetKeyState(WXK_SHIFT)) {
      // Shift + ENTER
      CallAfter(&wxDialog::EndModal, wxID_OK);
      return;
    }
    m_provider->ChangeDir(m_selection->path);
    PopulateView(m_selection->path);
  } else {
    CallAfter(&wxDialog::EndModal, wxID_OK);
  }
}

void FileBrowserDlg::OnUp(wxCommandEvent &event) {
  wxUnusedVar(event);
  m_provider->Up();
  PopulateView(m_provider->GetCurrentWorkingDir());
}

void FileBrowserDlg::OnHome(wxCommandEvent &event) {
  wxUnusedVar(event);
  m_provider->ChangeDir(m_provider->GetHome());
  PopulateView(m_provider->GetCurrentWorkingDir());
}

void FileBrowserDlg::OnKeyDown(wxKeyEvent &event) {
  if (event.GetKeyCode() == WXK_BACK) {
    // While a type-ahead search is active, backspace edits the search term
    // instead of navigating up.
    if (!m_typeAhead.empty()) {
      m_typeAhead.RemoveLast();
      if (!m_typeAhead.empty()) {
        JumpToTypeAhead(m_typeAhead);
      }
      return;
    }
    wxCommandEvent dummy;
    OnUp(dummy);
    return;
  } else if (event.GetKeyCode() == WXK_ESCAPE) {
    // First ESC cancels an in-progress search; a second one closes the dialog.
    if (!m_typeAhead.empty()) {
      m_typeAhead.clear();
      return;
    }
    CallAfter(&wxDialog::EndModal, wxID_CANCEL);
    return;
  }

  // Type-ahead: printable characters jump to the matching row.
  const wxChar uc = event.GetUnicodeKey();
  if (uc != WXK_NONE && uc >= 32 && !event.HasModifiers()) {
    const wxLongLong now = ::wxGetLocalTimeMillis();
    if ((now - m_lastKeyTime).GetValue() > 800) {
      m_typeAhead.clear(); // idle gap -> start a fresh search
    }
    m_lastKeyTime = now;
    m_typeAhead << uc;
    JumpToTypeAhead(m_typeAhead);
    return;
  }

  event.Skip(); // let arrows / Enter reach the data-view
}

void FileBrowserDlg::JumpToTypeAhead(const wxString &prefix) {
  const wxString needle = prefix.Lower();
  for (size_t row = 0; row < m_displayNames.size(); ++row) {
    if (m_displayNames[row].Lower().StartsWith(needle)) {
      const int r = static_cast<int>(row);
      const wxDataViewItem item = m_dvListCtrl->RowToItem(r);
      m_dvListCtrl->SelectRow(r);
      m_dvListCtrl->EnsureVisible(item);
      UpdateSelection(item); // keep m_selection / Select button in sync
      break;
    }
  }
}

void FileBrowserDlg::UpdateSelection(const wxDataViewItem &item) {
  if (!item.IsOk()) {
    m_selection = {};
    return;
  }

  auto row = m_dvListCtrl->ItemToRow(item);
  wxVariant v;
  m_dvListCtrl->GetValue(v, row, 0);
  wxDataViewIconText iconText;
  iconText << v;
  const wxString fullname = iconText.GetText();
  if (!m_files.contains(fullname)) {
    return;
  }
  m_selection = m_files[fullname];
  m_textCtrlCurrnentDir->SetValue(m_selection->path);

  if (!m_provider->IsDirsOnly() && m_selection->path.Lower().EndsWith(".svg")) {
    auto bmp = wxBitmapBundle::FromSVGFile(m_selection->path, wxSize(64, 64));
    if (bmp.IsOk()) {
      m_staticBitmapPreview->SetBitmap(bmp);
    }
  }
}

void FileBrowserDlg::OnSelect(wxCommandEvent &event) {
  wxUnusedVar(event);
  CallAfter(&wxDialog::EndModal,
            m_selection.has_value() ? wxID_OK : wxID_CANCEL);
}

void FileBrowserDlg::OnSelectUI(wxUpdateUIEvent &event) {
  if (!m_selection.has_value()) {
    event.Enable(false);
    return;
  }

  if (!m_provider->IsDirsOnly()) {
    event.Enable(m_selection->IsFile());
    return;
  }
  event.Enable(true);
}

void FileBrowserDlg::OnSelectionChanged(wxDataViewEvent &event) {
  UpdateSelection(event.GetItem());
}
