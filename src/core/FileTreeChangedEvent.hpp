#pragma once

#include <vector>
#include <wx/event.h>
#include <wx/string.h>

// Sent on the EventNotifier, from the worker thread of a file tree, when it is
// done with files that the user asked to make or to delete. GetTreeUID() is the
// tree (FileTreeCtrl::GetUID()). GetError() is empty when it worked, else one
// line for each path that failed. The tree lists GetRefreshDirs() again (the
// folders that changed, the outer ones first) and selects GetSelectPath() when
// it is not empty.
class FileTreeChangedEvent : public wxCommandEvent {
public:
  explicit FileTreeChangedEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  wxEvent *Clone() const override { return new FileTreeChangedEvent(*this); }

  int GetTreeUID() const { return m_treeUID; }
  void SetTreeUID(int uid) { m_treeUID = uid; }
  const wxString &GetError() const { return m_error; }
  void SetError(const wxString &error) { m_error = error; }
  const std::vector<wxString> &GetRefreshDirs() const { return m_refreshDirs; }
  void SetRefreshDirs(const std::vector<wxString> &dirs) {
    m_refreshDirs = dirs;
  }
  const wxString &GetSelectPath() const { return m_selectPath; }
  void SetSelectPath(const wxString &path) { m_selectPath = path; }

private:
  int m_treeUID{0};
  wxString m_error;
  std::vector<wxString> m_refreshDirs;
  wxString m_selectPath;
};
wxDECLARE_EVENT(wxEVT_FILETREE_CHANGED, FileTreeChangedEvent);
