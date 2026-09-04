#pragma once

#include "app/UI.hpp"
#include "core/Job.h"

#include <wx/dialog.h>

// Hand-written (not wxCrafter-generated) Add/Edit Job dialog. Lets the user
// define a timer-based job: a name, its type (raw shell command or a prompt
// sent to a configured agent), the command/prompt text, the run interval in
// hours, and whether to keep the terminal open once it completes.
class JobDlg : public JobDlgBase {
public:
  // `job` == nullptr for "New Job"; otherwise pre-fills from an existing one.
  JobDlg(wxWindow *parent, const JobDef *job);
  JobDef GetData() const;

private:
  void BuildUi();
  void PopulateAgents();
  void UpdateFieldsForType();
  wxString Validate() const;

  void OnJobTypeChanged(wxCommandEvent &event) override;
  void OnOk(wxCommandEvent &event) override;

protected:
  void OnOkUI(wxUpdateUIEvent &event) override;
};
