#pragma once

#include "UI.hpp"

#include "core/Workspace.h"

#include <wx/string.h>

class StartAgentDialog : public StartAgentDialogBase {
public:
  explicit StartAgentDialog(wxWindow *parent);
  ~StartAgentDialog() override;

  NewSessionRequest GetRequest() const;

  // The folder the user chose, empty when the default folder of the session is
  // used.
  wxString GetCustomWorkingDir() const;

  // Selects the agent with the given name, if present.
  bool SetSelectedClientName(const wxString &name);

  // Pre-sets the group field to `name` (does not need to be an existing group).
  void SetSelectedGroup(const wxString &name);

  // Pre-fills and selects the session name field (e.g. for "Duplicate
  // Session").
  void SetSessionName(const wxString &name);

protected:
  void OnUseCustomWorkingDirUI(wxUpdateUIEvent &event) override;
  void OnNameUpdated(wxCommandEvent &event) override;
  void OnBrowseWD(wxCommandEvent &event) override;
  void OnOkUI(wxUpdateUIEvent &event) override;
  // Shows the folder the agent starts in (MakeWorkingDir()), as it is now.
  void OnWorkingDirPreviewUI(wxUpdateUIEvent &event) override;
  // The folder the agent starts in: the one the user chose (with a folder of
  // the session name in it, when asked), or the default folder of the session.
  wxString MakeWorkingDir() const;
  wxString MakeGroupName() const;
  bool UsingCustomWorkingDir() const;

private:
  wxString m_lastGroupText; // The last group text that was allowed
  void PopulateClients();
  // Shows the "Run in tmux" box when the selected agent runs where tmux can.
  void UpdateTmuxChoice();
  // Fills the list of folders with the ones used before.
  void PopulateRecentDirs();
  wxString SelectedAgentId() const;
  // The separator of the folders of the machine the selected agent runs on:
  // '/' for a remote host or WSL, the one of this machine otherwise.
  wxString GetAgentPathSeparator() const;
};
