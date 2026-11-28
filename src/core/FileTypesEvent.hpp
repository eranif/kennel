#pragma once

#include <map>
#include <wx/event.h>
#include <wx/string.h>

// Sent on the EventNotifier, from a worker thread of a remote file tree, when
// it has read the first line of the files without an extension that it listed
// (for their icon, see Bitmaps::GetFileIcon()). GetTreeUID() is the tree that
// listed them (FileTreeCtrl::GetUID()), and GetLines() has the start of the
// first line of each file by its path. A file that could not be read is not in
// it. SideView gives it to that tree (FileTreeCtrl::ApplyFirstLines()).
class FileTypesEvent : public wxCommandEvent {
public:
  explicit FileTypesEvent(wxEventType type = wxEVT_NULL, int id = 0)
      : wxCommandEvent(type, id) {}
  wxEvent *Clone() const override { return new FileTypesEvent(*this); }

  int GetTreeUID() const { return m_treeUID; }
  void SetTreeUID(int uid) { m_treeUID = uid; }
  const std::map<wxString, wxString> &GetLines() const { return m_lines; }
  void SetLines(std::map<wxString, wxString> lines) {
    m_lines = std::move(lines);
  }

private:
  int m_treeUID{0};
  std::map<wxString, wxString> m_lines;
};
wxDECLARE_EVENT(wxEVT_FILETREE_TYPES_RESOLVED, FileTypesEvent);
