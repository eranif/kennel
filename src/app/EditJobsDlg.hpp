#pragma once

#include "core/Job.h"

#include <wx/dialog.h>

#include <vector>

class wxListBox;

// Hand-written (not wxCrafter-generated) "Manage Jobs" dialog: a simple
// list-CRUD view over the configured jobs, mirroring EditAgentsDlg's pattern
// but backed by an in-memory std::vector<JobDef> rather than owned pointers.
class EditJobsDlg : public wxDialog {
public:
  explicit EditJobsDlg(wxWindow *parent);

  const std::vector<JobDef> &GetJobs() const { return m_jobs; }

private:
  void RefreshList(int selectRow = 0);
  void EditSelection();
  // True if closing now would discard unsaved changes: either the user
  // isn't dirty, or they confirmed discarding in a Yes/No prompt.
  bool ConfirmDiscardChanges();

  void OnNewJob(wxCommandEvent &event);
  void OnEditJob(wxCommandEvent &event);
  void OnDeleteJob(wxCommandEvent &event);
  void OnRunNow(wxCommandEvent &event);
  void OnCancel(wxCommandEvent &event);
  void OnClose(wxCloseEvent &event);
  void OnCharHook(wxKeyEvent &event);
  void OnListDClick(wxCommandEvent &event);
  void OnEditUI(wxUpdateUIEvent &event);
  void OnDeleteUI(wxUpdateUIEvent &event);
  void OnRunNowUI(wxUpdateUIEvent &event);

  wxListBox *m_listBoxJobs{nullptr};
  std::vector<JobDef> m_jobs;
  bool m_dirty{false};
};
