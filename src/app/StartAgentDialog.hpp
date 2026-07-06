#pragma once

#include "UI.hpp"

#include "core/WorkspaceManager.h"

#include <wx/string.h>

class StartAgentDialog : public StartAgentDialogBase {
public:
  explicit StartAgentDialog(wxWindow *parent);
  ~StartAgentDialog() override;

  NewSessionRequest GetRequest() const;

  // Selects the agent with the given name, if present.
  bool SetSelectedClientName(const wxString &name);

  // Pre-sets the group field to `name` (does not need to be an existing group).
  void SetSelectedGroup(const wxString &name);

protected:
  void OnNameUpdated(wxCommandEvent &event) override;
  void OnBrowseWD(wxCommandEvent &event) override;
  void OnBrowseWdUI(wxUpdateUIEvent &event) override;
  void OnInnerFolder(wxCommandEvent &event) override;
  void OnOkUI(wxUpdateUIEvent &event) override;
  wxString MakeWorkingDir(bool checked) const;
  wxString MakeGroupName() const;

private:
  void PopulateClients();
  wxString SelectedAgentId() const;
};
