// The context menus of MainView, for the lists of the pages: they ask for a
// menu for a page (a session or a file), or for empty space. The commands of a
// group are in the menu of each of its sessions (AppendGroupMenu()).
#include "MainView.hpp"

#include "app/FilePage.hpp"
#include "app/PageRegistry.hpp"
#include "app/SessionPage.hpp"
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
  menu.AppendSeparator();
  menu.Append(wxID_CLOSE_ALL, _("Close All Files"));
  menu.Bind(
      wxEVT_MENU,
      [this](wxCommandEvent &) { CallAfter(&MainView::CloseAllFiles); },
      wxID_CLOSE_ALL);
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
    menu.AppendSeparator();
    menu.Append(wxID_ADD, _("New Terminal..."));
    menu.Bind(
        wxEVT_MENU, [this](wxCommandEvent &) { StartTerminal(); }, wxID_ADD);
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
    AppendGroupMenu(menu, currentGroupName);
  }
  on->PopupMenu(&menu);
}

void MainView::AppendGroupMenu(wxMenu &menu, const wxString &groupName) {
  auto *group = m_pages->GetGroup(groupName);
  CHECK_NOT_NULL_RETURN(group);
  if (!group->IsSessionGroup()) {
    return;
  }

  auto *groupMenu = new wxMenu;
  groupMenu->Append(XRCID("group-start-agent"), _("Start Agent..."));
  groupMenu->AppendSeparator();
  groupMenu->Append(XRCID("rename-group"), _("Rename Group..."));
  groupMenu->AppendSeparator();
  groupMenu->Append(XRCID("close-group"), _("Close Group"));
  groupMenu->AppendSeparator();
  groupMenu->Append(XRCID("refresh-sessions"), _("Refresh Group"));

  // The "Default" group must always exist and cannot be renamed.
  if (group->IsDefaultGroup()) {
    groupMenu->Enable(XRCID("rename-group"), false);
  }

  // The group is looked up again when the item is chosen: it is only
  // reachable by name, since the PageRegistry owns it.
  groupMenu->Bind(
      wxEVT_MENU,
      [groupName, this](wxCommandEvent &) {
        StartAgent(wxEmptyString, groupName);
      },
      XRCID("group-start-agent"));
  groupMenu->Bind(
      wxEVT_MENU,
      [groupName, this](wxCommandEvent &) {
        RenameGroup(m_pages->GetGroup(groupName));
      },
      XRCID("rename-group"));
  groupMenu->Bind(
      wxEVT_MENU,
      [groupName, this](wxCommandEvent &) {
        CallAfter(&MainView::DeleteGroupByName, groupName);
      },
      XRCID("close-group"));
  groupMenu->Bind(
      wxEVT_MENU,
      [groupName, this](wxCommandEvent &) {
        RefreshGroup(m_pages->GetGroup(groupName));
      },
      XRCID("refresh-sessions"));
  menu.AppendSeparator();
  // The name is escaped: an "&" in it is not a mnemonic.
  wxString escaped = groupName;
  escaped.Replace("&", "&&");
  menu.AppendSubMenu(groupMenu, wxString::Format(_("Group \"%s\""), escaped));
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
