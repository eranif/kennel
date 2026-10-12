#include "app/FilePage.hpp"

#include "app/EditFileDlg.hpp"
#include "app/MainFrame.h"
#include "core/AppManager.h"
#include "core/SftpClient.h"
#include <wx/app.h>
#include <wx/filename.h>
#include <wx/frame.h>
#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/utils.h>

#include <thread>
namespace {
const int ID_SAVER = wxWindow::NewControlId();
const int ID_RELOAD = wxWindow::NewControlId();
const int ID_CLOSE = wxWindow::NewControlId();
} // namespace

FilePage::FilePage(wxWindow *parent, const wxString &path,
                   const wxTerminalTheme &theme,
                   const RemoteHostDetails &remoteHost)
    : wxPanel(parent), m_path{path}, m_remoteHost{remoteHost} {
  m_toolbar = new wxAuiToolBar(this);
  AppManager::Get().GetBitmaps().Get("save", false);
  m_toolbar->AddTool(ID_CLOSE, _("Close"),
                     AppManager::Get().GetBitmaps().Get("close", false),
                     _("Close The Current File"));
  m_toolbar->AddSeparator();
  m_toolbar->AddTool(ID_SAVER, _("Save"),
                     AppManager::Get().GetBitmaps().Get("save", false),
                     _("Save File Modifications"));
  m_toolbar->AddTool(ID_RELOAD, _("Reload"),
                     AppManager::Get().GetBitmaps().Get("reload", false),
                     _("Reload The Current File"));
  m_toolbar->Bind(wxEVT_TOOL, &FilePage::OnSave, this, ID_SAVER);
  m_toolbar->Bind(wxEVT_TOOL, &FilePage::OnReload, this, ID_RELOAD);
  m_toolbar->Bind(wxEVT_UPDATE_UI, &FilePage::OnSaveUI, this, ID_SAVER);
  m_toolbar->Bind(wxEVT_TOOL, &FilePage::OnClose, this, ID_CLOSE);
  m_toolbar->Realize();

  m_editor = new Editor(this, EditFileDlg::LangFromPath(path), theme);
  SetSizer(new wxBoxSizer(wxVERTICAL));
  GetSizer()->Add(m_toolbar, wxSizerFlags(0).Expand());
  GetSizer()->Add(m_editor, wxSizerFlags(1).Expand());

  wxStyledTextCtrl *ctrl = m_editor->GetCtrl();
  m_acceleratorInterceptor = std::make_unique<AcceleratorInterceptor>(ctrl);
  ctrl->Bind(wxEVT_KEY_DOWN, &FilePage::OnKeyDown, this);
  ctrl->Bind(wxEVT_STC_SAVEPOINTLEFT, &FilePage::OnSavePointChanged, this);
  ctrl->Bind(wxEVT_STC_SAVEPOINTREACHED, &FilePage::OnSavePointChanged, this);
  ctrl->Bind(wxEVT_STC_UPDATEUI, &FilePage::OnEditorUpdateUI, this);
  Bind(wxEVT_FIND, &FilePage::OnFind, this);
  Bind(wxEVT_FIND_NEXT, &FilePage::OnFind, this);
  Bind(wxEVT_FIND_REPLACE, &FilePage::OnFindReplace, this);
  Bind(wxEVT_FIND_REPLACE_ALL, &FilePage::OnFindReplaceAll, this);
  Bind(wxEVT_FIND_CLOSE, &FilePage::OnFindClose, this);
}

FilePage::~FilePage() {
  DestroyFindDialog();
  if (m_saveThread.joinable()) {
    m_saveThread.join();
  }
}

bool FilePage::LoadLocal() { return m_editor->LoadFile(m_path); }

void FilePage::LoadRemote(const wxString &text, bool editable) {
  m_editor->SetText(text);
  m_editor->SetEditable(editable);
}

wxString FilePage::GetDisplayName() const {
  const wxString name =
      IsRemote() ? m_path.AfterLast('/') : wxFileName(m_path).GetFullName();
  return name.empty() ? m_path : name;
}

bool FilePage::SaveAsync() {
  if (!IsRemote() || m_saving || !m_editor->IsEditable()) {
    return false;
  }
  if (m_saveThread.joinable()) {
    m_saveThread.join(); // The previous upload finished; reap its thread.
  }

  m_saving = true;
  const std::string bytes = m_editor->GetText().ToStdString(wxConvUTF8);

  // Optimistically mark the document clean. Anything typed while the upload
  // runs makes it dirty again on its own, and FileSaved() re-marks it dirty if
  // the upload fails.
  m_editor->GetCtrl()->SetSavePoint();

  FileEvent event{wxEVT_FILE_SAVE_STARTED};
  event.SetFilePath(m_path);
  event.SetRemoteHostDetails(m_remoteHost);
  AddPendingEvent(event);

  // The thread gets copies of the data it needs, never touches the UI, and
  // reports back by posting an event to this page, which stays alive until the
  // destructor has joined the thread.
  m_saveThread =
      std::thread([this, remoteHost = m_remoteHost, path = m_path, bytes]() {
        CallAfter(&FilePage::FileSaved,
                  SftpClient::WriteFile(remoteHost.host, remoteHost.user, path,
                                        bytes));
      });
  return true;
}

void FilePage::FileSaved(const Status &status) {
  m_saving = false;
  if (!status.ok() && !IsModified()) {
    MarkModified(); // The file was not saved after all.
  }

  // Always report the outcome, and do it before the (modal) error dialog so
  // that MainView's bookkeeping (activity indicator, close-after-save) does
  // not wait for the user to dismiss it.
  FileEvent event{wxEVT_FILE_SAVE_DONE};
  event.SetFilePath(m_path);
  event.SetRemoteHostDetails(m_remoteHost);
  event.SetStatusCode(status);
  AddPendingEvent(event);

  if (!status.ok()) {
    ::wxMessageBox(status.message(), "Kennel", wxOK | wxICON_ERROR, this);
  }
}

void FilePage::MarkModified() {
  // wxStyledTextCtrl::MarkDirty() is not implemented, so make an edit that
  // leaves the text unchanged but moves the document off its save point.
  wxStyledTextCtrl *ctrl = m_editor->GetCtrl();
  EditableLocker editable{ctrl};
  ctrl->BeginUndoAction();
  ctrl->InsertText(0, " ");
  ctrl->DeleteRange(0, 1);
  ctrl->EndUndoAction();
}

bool FilePage::Save() {
  if (IsRemote()) {
    return false; // Remote files save through SaveAsync().
  }
  if (!m_editor->CanSave()) {
    return false;
  }
  if (!m_editor->Save()) {
    ::wxMessageBox(wxString() << _("Could not write file:\n") << m_path,
                   "Kennel", wxOK | wxICON_ERROR, this);
    return false;
  }
  return true;
}

void FilePage::FocusEditor() { m_editor->GetCtrl()->SetFocus(); }

void FilePage::OnKeyDown(wxKeyEvent &event) {
  // Cmd+S on macOS, Ctrl+S elsewhere.
  if (event.GetKeyCode() == 'S' && event.GetModifiers() == wxMOD_CONTROL) {
    if (IsRemote()) {
      SaveAsync();
    } else {
      Save();
    }
    return;
  }
  // Replace (Ctrl+H). Find (Ctrl+F) is in AcceleratorInterceptor, as it is
  // the same for the terminals. On macOS both are menu items, which get the
  // keys first (and Cmd+H hides the application).
#ifndef __WXOSX__
  if (event.GetModifiers() == wxMOD_CONTROL && event.GetKeyCode() == 'H') {
    // As the menu item: nothing to replace in a read-only file.
    if (CanReplace()) {
      ShowFindDialog(true);
    }
    return;
  }
#endif
  event.Skip();
}

void FilePage::OnEditorUpdateUI(wxStyledTextEvent &event) {
  event.Skip();
  // Also sent while loading, when the page is not showing yet.
  if (IsShownOnScreen()) {
    PublishStatus();
  }
}

void FilePage::PublishStatus() {
  wxStyledTextCtrl *ctrl = m_editor->GetCtrl();
  const int pos = ctrl->GetCurrentPos();
  wxString text =
      wxString::Format(_("Ln %d, Col %d"), ctrl->LineFromPosition(pos) + 1,
                       ctrl->GetColumn(pos) + 1);
  const int from = ctrl->GetSelectionStart();
  const int to = ctrl->GetSelectionEnd();
  if (from != to) {
    // Characters, not bytes: the text is UTF-8.
    text << wxString::Format(_(" (%d selected)"),
                             ctrl->CountCharacters(from, to));
  }
  GetMainFrame()->SetEditorStatus(text);
}

void FilePage::OnSavePointChanged(wxStyledTextEvent &event) {
  event.Skip();
  wxFrame *frame = static_cast<wxFrame *>(wxTheApp->GetTopWindow());
  frame->SetLabel((m_editor->GetCtrl()->IsModified()
                       ? wxString::FromUTF8("💾 ") // U+1F4BE floppy disk
                       : wxString{}) +
                  m_path);
}

void FilePage::OnSave(wxCommandEvent &event) {
  wxUnusedVar(event);
  if (m_editor && m_editor->IsModified())
    m_editor->Save();
}

void FilePage::OnSaveUI(wxUpdateUIEvent &event) {
  event.Enable(m_editor && m_editor->IsModified());
}

void FilePage::OnReload(wxCommandEvent &event) {
  wxUnusedVar(event);
  if (m_saving) {
    return; // The upload is not done: what is reloaded now may be the old copy.
  }
  if (IsModified() &&
      ::wxMessageBox(
          wxString::Format(_("Discard your changes to %s and reload it?"),
                           GetDisplayName()),
          "Kennel", wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) != wxYES) {
    return;
  }

  if (IsRemote()) {
    // Read on a worker thread by MainView, which then replaces the text.
    GetMainView()->ReloadRemoteFile(m_remoteHost, m_path);
    return;
  }

  if (!LoadLocal()) {
    ::wxMessageBox(wxString::Format(_("Could not open %s"), m_path), "Kennel",
                   wxOK | wxICON_ERROR, this);
  }
}

void FilePage::OnClose(wxCommandEvent &event) {
  wxUnusedVar(event);
  GetMainView()->CallAfter(&MainView::CloseFileByKey, GetKey());
}

// ---------------------------------------------------------------------------
// Find and Replace
// ---------------------------------------------------------------------------

void FilePage::ShowFindDialog(bool replace) {
  replace = replace && CanReplace();

  // The selected text, when it is on one line, is what the user looks for.
  const wxString selected = m_editor->GetCtrl()->GetSelectedText();
  if (!selected.empty() && selected.Find('\n') == wxNOT_FOUND &&
      selected.Find('\r') == wxNOT_FOUND) {
    m_findData.SetFindString(selected);
  }

  // A new dialog every time: it reads the text to look for when it is created.
  DestroyFindDialog();
  m_findDlg = new wxFindReplaceDialog(
      this, &m_findData, replace ? _("Find and Replace") : _("Find"),
      replace ? wxFR_REPLACEDIALOG : 0);
  m_findDlgReplace = replace;
  m_findDlg->Show();
}

void FilePage::DestroyFindDialog() {
  if (m_findDlg != nullptr) {
    m_findDlg->Destroy();
    m_findDlg = nullptr;
  }
}

void FilePage::TakeFindData(const wxFindDialogEvent &event) {
  m_findData.SetFindString(event.GetFindString());
  m_findData.SetReplaceString(event.GetReplaceString());
  m_findData.SetFlags(event.GetFlags());
}

int FilePage::FindSearchFlags() const {
  const int flags = m_findData.GetFlags();
  int result = 0;
  if (flags & wxFR_MATCHCASE) {
    result |= wxSTC_FIND_MATCHCASE;
  }
  if (flags & wxFR_WHOLEWORD) {
    result |= wxSTC_FIND_WHOLEWORD;
  }
  return result;
}

bool FilePage::FindIn(bool forward) {
  const wxString what = m_findData.GetFindString();
  if (what.empty()) {
    return false;
  }

  wxStyledTextCtrl *ctrl = m_editor->GetCtrl();
  ctrl->SetSearchFlags(FindSearchFlags());
  const int length = ctrl->GetLength();
  // A target that ends before it starts is searched backwards.
  auto search = [&](int start, int end) {
    ctrl->SetTargetStart(start);
    ctrl->SetTargetEnd(end);
    return ctrl->SearchInTarget(what);
  };

  int found;
  bool wrapped = false;
  if (forward) {
    found = search(ctrl->GetSelectionEnd(), length);
    if (found == wxNOT_FOUND) {
      wrapped = true;
      found = search(0, length);
    }
  } else {
    found = search(ctrl->GetSelectionStart(), 0);
    if (found == wxNOT_FOUND) {
      wrapped = true;
      found = search(length, 0);
    }
  }

  if (found == wxNOT_FOUND) {
    GetMainFrame()->SetActivityText(
        wxString::Format(_("\"%s\" was not found"), what));
    return false;
  }

  ctrl->SetSelection(ctrl->GetTargetStart(), ctrl->GetTargetEnd());
  ctrl->EnsureCaretVisible();
  if (wrapped) {
    GetMainFrame()->SetActivityText(
        forward ? _("Search continued from the top")
                : _("Search continued from the bottom"));
  } else {
    GetMainFrame()->ClearActivityText();
  }
  return true;
}

void FilePage::FindNext(bool forward) {
  if (m_findData.GetFindString().empty()) {
    ShowFindDialog(false);
    return;
  }
  FindIn(forward);
}

void FilePage::OnFind(wxFindDialogEvent &event) {
  TakeFindData(event);
  FindIn((event.GetFlags() & wxFR_DOWN) != 0);
}

void FilePage::OnFindReplace(wxFindDialogEvent &event) {
  TakeFindData(event);
  if (!CanReplace()) {
    return;
  }

  // The match that is selected is replaced; with anything else selected this
  // finds the next match first, as the user expects.
  wxStyledTextCtrl *ctrl = m_editor->GetCtrl();
  const wxString what = m_findData.GetFindString();
  // Search the selection with the options of the dialog (whole word, case):
  // it is a match only if the match is all of it.
  bool matches = false;
  if (!what.empty()) {
    const int from = ctrl->GetSelectionStart();
    const int to = ctrl->GetSelectionEnd();
    ctrl->SetSearchFlags(FindSearchFlags());
    ctrl->SetTargetStart(from);
    ctrl->SetTargetEnd(to);
    matches = from != to && ctrl->SearchInTarget(what) != wxNOT_FOUND &&
              ctrl->GetTargetStart() == from && ctrl->GetTargetEnd() == to;
  }
  if (matches) {
    ctrl->ReplaceSelection(m_findData.GetReplaceString());
  }
  FindIn(true);
}

void FilePage::OnFindReplaceAll(wxFindDialogEvent &event) {
  TakeFindData(event);
  const wxString what = m_findData.GetFindString();
  if (!CanReplace() || what.empty()) {
    return;
  }

  wxStyledTextCtrl *ctrl = m_editor->GetCtrl();
  ctrl->SetSearchFlags(FindSearchFlags());
  int count = 0;
  int from = 0;
  // One undo step for all of them.
  ctrl->BeginUndoAction();
  for (;;) {
    ctrl->SetTargetStart(from);
    ctrl->SetTargetEnd(ctrl->GetLength());
    if (ctrl->SearchInTarget(what) == wxNOT_FOUND) {
      break;
    }
    const int start = ctrl->GetTargetStart();
    // The length (in bytes) of the replacement: the search goes on after it.
    from = start + ctrl->ReplaceTarget(m_findData.GetReplaceString());
    ++count;
  }
  ctrl->EndUndoAction();

  GetMainFrame()->SetActivityText(
      count == 0 ? wxString::Format(_("\"%s\" was not found"), what)
                 : wxString::Format(wxPLURAL("Replaced %d occurrence",
                                             "Replaced %d occurrences", count),
                                    count));
}

void FilePage::OnFindClose(wxFindDialogEvent &event) {
  wxUnusedVar(event);
  DestroyFindDialog();
  FocusEditor();
}
