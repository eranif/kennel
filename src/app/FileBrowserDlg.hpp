#pragma once

#include "UI.hpp"
#include "app/FileProvider.hpp"

#include <wx/longlong.h>

#include <memory>
#include <optional>

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
