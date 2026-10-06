// The context menus of MainView, shared by the tree and the flat list: both
// ask for a menu for a page (a session or a file), and only the tree has groups
// to ask for.
#include "MainView.hpp"

#include "app/FilePage.hpp"
#include "app/SessionPage.hpp"
#include "app/TreeView.hpp"
#include "core/Helpers.h"

#include <wx/menu.h>
#include <wx/textdlg.h>
#include <wx/xrc/xmlres.h>

void MainView::ShowPageMenu(wxWindow *page, wxWindow *on) {
  if (auto *session = dynamic_cast<SessionPage *>(page)) {
    ShowSessionMenu(session, on);
  } else if (auto *file = dynamic_cast<FilePage *>(page)) {
    ShowFileMenu(file, on);
  }
}

void MainView::ShowFileMenu(FilePage *page, wxWindow *on) {
  CHECK_NOT_NULL_RETURN(page);
  const wxString key = page->GetKey();

  wxMenu menu;
  menu.Append(wxID_CLOSE, _("Close"));
  menu.Bind(
      wxEVT_MENU,
      [key, this](wxCommandEvent &) {
        // Deferred: see RemovePage.
        CallAfter(&MainView::CloseFileByKey, key);
      },
      wxID_CLOSE);
  on->PopupMenu(&menu);
}

void MainView::ShowSessionMenu(SessionPage *page, wxWindow *on) {
  CHECK_NOT_NULL_RETURN(page);
  const wxString sessionName = page->GetSession().name;
  const wxString currentGroupName = page->GetSession().groupName;

  wxMenu menu;
  menu.Append(XRCID("session-group-close-session"), _("Close"),
              _("Close Session"));
  menu.Bind(
      wxEVT_MENU,
      [currentGroupName, sessionName, this](wxCommandEvent &) {
        // Deferred: see RemovePage.
        CallAfter(&MainView::CloseSession, currentGroupName, sessionName);
      },
      XRCID("session-group-close-session"));

  menu.AppendSeparator();
  if (page->IsPlainTerminal()) {
    menu.Append(XRCID("rename-terminal"), _("Rename Terminal"),
                _("Rename Terminal"));
    menu.Bind(
        wxEVT_MENU, [page, this](wxCommandEvent &) { RenameSession(page); },
        XRCID("rename-terminal"));
  } else {
    menu.Append(XRCID("rename-session"), _("Rename..."), _("Rename Session"));
    menu.Bind(
        wxEVT_MENU, [page, this](wxCommandEvent &) { RenameSession(page); },
        XRCID("rename-session"));

    menu.Append(XRCID("duplicate-session"), _("Duplicate..."),
                _("Duplicate Session"));
    menu.Bind(
        wxEVT_MENU, [page, this](wxCommandEvent &) { DuplicateSession(page); },
        XRCID("duplicate-session"));
    menu.AppendSeparator();

    wxMenu *moveMenu = new wxMenu;
    wxArrayString groups;
    for (const wxString &name : GetGroupNames()) {
      if (name != currentGroupName) {
        groups.Add(name);
      }
    }

    if (!groups.empty()) {
      for (const wxString &groupName : groups) {
        int id = wxXmlResource::GetXRCID(
            wxString::Format("move-to-group-%s", groupName));
        moveMenu->Append(id, groupName,
                         wxString::Format(_("Move to group: %s"), groupName));
        moveMenu->Bind(
            wxEVT_MENU,
            [groupName, sessionName, currentGroupName, this](wxCommandEvent &) {
              // Deferred: see RemovePage.
              CallAfter([this, sessionName, currentGroupName, groupName] {
                MoveSessionToGroup(sessionName, currentGroupName, groupName);
              });
            },
            id);
      }
      moveMenu->AppendSeparator();
    }
    moveMenu->Append(XRCID("create-new-group"), _("New Group..."));
    moveMenu->Bind(
        wxEVT_MENU,
        [sessionName, currentGroupName, this](wxCommandEvent &) {
          wxString newGroup = ::wxGetTextFromUser(_("New Group Name"), "Kennel",
                                                  wxEmptyString, this);
          if (newGroup.empty() || newGroup == currentGroupName)
            return;
          // Deferred: see RemovePage.
          CallAfter([this, sessionName, currentGroupName, newGroup] {
            MoveSessionToGroup(sessionName, currentGroupName, newGroup);
          });
        },
        XRCID("create-new-group"));
    menu.AppendSubMenu(moveMenu, _("Move To Group"));
  }
  on->PopupMenu(&menu);
}

void MainView::ShowGroupMenu(const wxString &groupName, wxWindow *on) {
  auto *group = m_treeView->GetGroup(groupName);
  CHECK_NOT_NULL_RETURN(group);

  wxMenu menu;
  if (group->IsFilesGroup()) {
    menu.Append(wxID_CLOSE_ALL, _("Close All Files"));
    menu.Bind(
        wxEVT_MENU,
        [this](wxCommandEvent &) { CallAfter(&MainView::CloseAllFiles); },
        wxID_CLOSE_ALL);
  } else if (group->IsTerminalsGroup()) {
    menu.Append(wxID_ADD, _("New Terminal..."));
    menu.Bind(
        wxEVT_MENU, [this](wxCommandEvent &) { StartTerminal(); }, wxID_ADD);
  } else {
    menu.Append(wxID_ADD, _("Start Agent..."));
    menu.AppendSeparator();
    menu.Append(XRCID("rename-group"), _("Rename Group..."));
    menu.AppendSeparator();
    menu.Append(wxID_CLOSE_ALL, _("Close Group"));
    menu.AppendSeparator();
    menu.Append(XRCID("refresh-sessions"), _("Refresh"));

    // The "Default" group must always exist and cannot be renamed.
    if (group->IsDefaultGroup()) {
      menu.Enable(XRCID("rename-group"), false);
    }

    // The group is looked up again when the item is chosen: it is only
    // reachable by name, since the tree owns it.
    menu.Bind(
        wxEVT_MENU,
        [groupName, this](wxCommandEvent &) {
          StartAgent(wxEmptyString, groupName);
        },
        wxID_ADD);
    menu.Bind(
        wxEVT_MENU,
        [groupName, this](wxCommandEvent &) {
          RenameGroup(m_treeView->GetGroup(groupName));
        },
        XRCID("rename-group"));
    menu.Bind(
        wxEVT_MENU,
        [groupName, this](wxCommandEvent &) {
          CallAfter(&MainView::DeleteGroupByName, groupName);
        },
        wxID_CLOSE_ALL);
    menu.Bind(
        wxEVT_MENU,
        [groupName, this](wxCommandEvent &) {
          RefreshGroup(m_treeView->GetGroup(groupName));
        },
        XRCID("refresh-sessions"));
  }
  on->PopupMenu(&menu);
}

void MainView::ShowBackgroundMenu(wxWindow *on) {
  wxMenu menu;
  menu.Append(wxID_ADD, _("Start Agent..."));
  menu.Append(XRCID("start-terminal"), _("New Terminal..."));
  menu.Bind(wxEVT_MENU, [this](wxCommandEvent &) { StartAgent(); }, wxID_ADD);
  menu.Bind(
      wxEVT_MENU, [this](wxCommandEvent &) { StartTerminal(); },
      XRCID("start-terminal"));
  on->PopupMenu(&menu);
}
