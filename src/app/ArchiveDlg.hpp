#pragma once

#include "UI.hpp"
#include "core/Workspace.h"

#include <vector>
#include <wx/event.h>

// The user asked to restore / delete an archived session. GetInt() is the
// index of the session in the list given to SetItems(). The dialog sends them
// to itself; the one that shows it handles them (and gives the dialog the new
// list with SetItems()).
wxDECLARE_EVENT(wxEVT_ARCHIVE_RESTORE, wxCommandEvent);
wxDECLARE_EVENT(wxEVT_ARCHIVE_DELETE, wxCommandEvent);

// Lists the archived sessions, with a filter box. The columns can be sorted
// (the newest is first); they get as wide as their content.
class ArchiveDlg : public ArchiveDlgBase {
public:
  explicit ArchiveDlg(wxWindow *parent);
  ~ArchiveDlg() override;

  // Replaces the list.
  void SetItems(const std::vector<ArchivedSession> &items);

private:
  // Fills the list with the items that match the filter.
  void Populate();
  // The index (in m_items) of the selected row, or -1.
  int SelectedIndex() const;
  void SendRequest(const wxEventTypeTag<wxCommandEvent> &type);

  void OnFilter(wxCommandEvent &event);
  void OnCharHook(wxKeyEvent &event);
  void OnRestore(wxCommandEvent &event);
  void OnDelete(wxCommandEvent &event);
  void OnClose(wxCommandEvent &event);
  void OnActivated(wxDataViewEvent &event);
  void OnButtonUI(wxUpdateUIEvent &event);

  std::vector<ArchivedSession> m_items;
};
