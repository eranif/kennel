#pragma once

#include "UI.hpp"
#include "core/Workspace.h"

#include <vector>

// One entry of the "Goto Anything" dialog: what the user sees and what to do
// when it is chosen (the owner of the dialog does it, see MainFrame).
struct GotoItem {
  enum class Kind {
    Menu,        // A menu item: `menuId`
    Page,        // Show a session, a terminal or a file: `ref`
    Restart,     // Restart the session or terminal `ref`
    StartReview, // Start a review buddy for the session `ref` with `agentName`
    StopReview,  // Close the review buddy of the session `ref`
  };
  Kind kind{Kind::Menu};
  wxString label;    // "Select Terminal: name", "File > Open File...", ...
  wxString shortcut; // The keyboard shortcut, if it has one
  int menuId{0};
  bool checkable{false}; // A menu item with a check mark
  bool checked{false};
  SessionRef ref;
  wxString agentName;
};

// A search box above a list. What is typed filters the list (every word must be
// in the label); Up / Down move in the list, Enter chooses and Esc closes. It
// has no buttons.
class GotoAnythingDlg : public GotoAnythingDlgBase {
public:
  explicit GotoAnythingDlg(wxWindow *parent);
  ~GotoAnythingDlg() override;

  void SetItems(std::vector<GotoItem> items);
  // The entry the user chose; null if the dialog was closed without one.
  const GotoItem *GetChosen() const;

  int ShowModal() override;

private:
  // Fills the list with the items that match what is typed.
  void Populate();
  // The shortcut column fits its text, the action column takes the rest.
  void FitColumns();
  void OnListSize(wxSizeEvent &event);
  void Accept();
  void MoveSelection(int delta);

  void OnText(wxCommandEvent &event);
  void OnCharHook(wxKeyEvent &event);
  void OnActivated(wxDataViewEvent &event);

  std::vector<GotoItem> m_items;
  int m_chosen{-1}; // An index in m_items
};
