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
}

FilePage::~FilePage() {
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
  event.Skip();
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
