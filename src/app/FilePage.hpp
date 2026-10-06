#pragma once

#include "app/AcceleratorInterceptor.h"
#include "app/Editor.h"
#include "app/FileEvent.hpp"
#include "terminal_theme.h"

#include <memory>
#include <thread>
#include <wx/panel.h>

// A file shown in the main view, opened from a path clicked in a terminal.
// Both kinds are edited here and saved with Save / Ctrl+S: a local file is
// written to disk, a remote file is written back over SFTP.
class FilePage : public wxPanel {
public:
  // `remoteHost` is empty for a local file. The page's key (GetKey()) is
  // FileEvent::MakeKey(path, remoteHost), so re-opening the same file can
  // reselect the existing page.
  FilePage(wxWindow *parent, const wxString &path, const wxTerminalTheme &theme,
           const RemoteHostDetails &remoteHost = {});
  // Joins the save thread, if one is running.
  ~FilePage() override;

  // Local file: loads `path` from disk. Returns false on failure.
  bool LoadLocal();
  // Remote file: shows `text`. `editable` false shows it read-only (used when
  // the bytes could not be decoded losslessly, so saving would corrupt them).
  void LoadRemote(const wxString &text, bool editable = true);

  wxString GetKey() const { return FileEvent::MakeKey(m_path, m_remoteHost); }
  const wxString &GetPath() const { return m_path; }
  const RemoteHostDetails &GetRemoteHostDetails() const { return m_remoteHost; }
  wxString GetDisplayName() const;
  bool IsRemote() const { return !m_remoteHost.host.empty(); }
  bool IsModified() const { return m_editor->GetCtrl()->GetModify(); }
  bool CanSave() const {
    return IsRemote() ? m_editor->IsEditable() : m_editor->CanSave();
  }
  // Writes a local file back to disk; reports failure with a message box.
  bool Save();
  // Remote files only: uploads the file on the page's save thread and returns
  // immediately. Returns false if nothing was started (a local file, a
  // read-only one, or a save already in flight). wxEVT_FILE_SAVE_STARTED is
  // sent now, wxEVT_FILE_SAVE_DONE when the upload finishes.
  bool SaveAsync();
  bool IsSaving() const { return m_saving; }

  void ApplyTheme(const wxTerminalTheme &theme) { m_editor->SetTheme(theme); }
  void FocusEditor();

private:
  void FileSaved(const Status &status);

  // Puts the document back into the "has unsaved changes" state.
  void MarkModified();
  void OnKeyDown(wxKeyEvent &event);
  void OnSavePointChanged(wxStyledTextEvent &event);

  wxString m_path;
  RemoteHostDetails m_remoteHost;
  bool m_saving{false};
  // Joinable, and owned by the page so the page (the event sink) is alive for
  // as long as the thread runs: the destructor joins it.
  std::thread m_saveThread;
  Editor *m_editor{nullptr};
  // Lets Alt+Left/Right (and the other global shortcuts) work while the
  // editor has focus.
  std::unique_ptr<AcceleratorInterceptor> m_acceleratorInterceptor;
};
