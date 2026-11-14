#pragma once

#include "UI.hpp"
#include "app/AsyncGuard.hpp"
#include "core/Workspace.h"

#include <map>
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

  // What the machine said about a tmux session that an archived session kept.
  enum class TmuxState {
    kRunning, // Still there: a restore attaches to it
    kGone,    // Ended: a restore starts a new one
    kUnknown, // The machine could not be asked (or has no tmux)
  };
  // Shows the state in the row of the session whose tmux session is `name`. A
  // session with a tmux session and no state yet shows "Checking...".
  void SetTmuxState(const wxString &name, TmuxState state);
  // For a worker thread that answers later (see AsyncGuard.hpp): false once the
  // dialog is gone.
  AliveFlag GetAliveFlag() const { return m_alive; }

private:
  // Fills the list with the items that match the filter.
  void Populate();
  // The text of the tmux column for `session`: empty when it has no tmux
  // session.
  wxString TmuxText(const Session &session) const;
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
  // By the name of the tmux session.
  std::map<wxString, TmuxState> m_tmuxStates;
  AliveFlag m_alive{MakeAliveFlag()};
};
