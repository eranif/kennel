#pragma once

#include "UI.hpp"

#include "core/Workspace.h"

#include <vector>
#include <wx/string.h>

// File > Broadcast: the user picks sessions (all of them are checked at
// first), types a message, and Send gives it to each of them (the notebook
// types it into their agents: MainNotebook::BroadcastPrompt()). The layout is
// BroadcastDlgBase, in UI.wxcp.
class BroadcastDlg : public BroadcastDlgBase {
public:
  // A session that can get the message, and how the list shows it.
  struct Target {
    SessionRef ref;
    wxString label;
  };

  BroadcastDlg(wxWindow *parent, const std::vector<Target> &targets);
  ~BroadcastDlg() override;

  // The sessions that are checked.
  std::vector<SessionRef> GetSelectedSessions() const;
  // The message as one line: in most agents a new line would send the part
  // before it at once, so the lines are joined with spaces.
  wxString GetMessage() const;

protected:
  void OnCheckAll(wxCommandEvent &event) override;
  void OnUncheckAll(wxCommandEvent &event) override;
  void OnSendUI(wxUpdateUIEvent &event) override;

private:
  // Checks or unchecks every session of the list.
  void CheckAll(bool check);

  std::vector<Target> m_targets; // In the order of the list
};
