#pragma once

#include "app/AcceleratorInterceptor.h"
#include "app/Editor.h"
#include "app/FileEvent.hpp"
#include "app/FileViewState.hpp"
#include "core/Account.h"
#include "terminal_theme.h"

#include <memory>
#include <thread>
#include <wx/aui/auibar.h>
#include <wx/panel.h>

class SearchBar;

// Sent on the EventNotifier when the user changes Wrap Lines: every file page
// shows the setting of the preferences.
wxDECLARE_EVENT(wxEVT_WRAP_LINES_CHANGED, wxCommandEvent);

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
  // Shows `text` read-only, as a text that is not a file (a git diff): the
  // page is not kept in the session, and Reload() does nothing. `path` of the
  // page only names it (its extension decides the language).
  void LoadText(const wxString &text);
  bool IsTransient() const { return m_transient; }

  wxString GetKey() const { return FileEvent::MakeKey(m_path, m_remoteHost); }
  const wxString &GetPath() const { return m_path; }
  const RemoteHostDetails &GetRemoteHostDetails() const { return m_remoteHost; }
  wxString GetDisplayName() const;
  // The folder of the file: a path for a local file, "host:path" for a remote
  // one.
  wxString GetFolder() const;
  // Whose file it is: a remote host, a WSL distribution (a file on the share
  // of the distro) or this machine.
  Account GetAccount() const;
  // The path in the notation of GetAccount(): a Linux path for a WSL file, the
  // path of the page otherwise. It is what a FileTreeCtrl of the account shows.
  wxString GetAccountPath() const;
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
  // Gives the toolbar the style of the theme of the application.
  void ApplyToolBarStyle();
  void FocusEditor();
  bool EditorHasFocus() const { return m_editor->GetCtrl()->HasFocus(); }
  // Loads the file again (asks first when there are unsaved changes).
  void Reload();
  // The Edit menu: these act on the text of the editor.
  bool CanUndo() const { return m_editor->GetCtrl()->CanUndo(); }
  bool CanRedo() const { return m_editor->GetCtrl()->CanRedo(); }
  void Undo() { m_editor->GetCtrl()->Undo(); }
  void Redo() { m_editor->GetCtrl()->Redo(); }
  void Copy() { m_editor->GetCtrl()->Copy(); }
  void Paste() { m_editor->GetCtrl()->Paste(); }
  void SelectAll() { m_editor->GetCtrl()->SelectAll(); }
  void DeleteLine() { m_editor->GetCtrl()->LineDelete(); }
  void DuplicateLine() { m_editor->GetCtrl()->LineDuplicate(); }
  // Puts the caret at `line` and `column` (both from 1; the column is a byte
  // offset in the line, as compilers count it) and shows that line in the
  // middle of the editor. A line or a column out of range goes to the nearest
  // place that exists.
  void GotoLine(int line, int column = 1);
  // Saved before a move of the caret that may be taken back (GotoLine() as the
  // user types), and put back with SetViewState(). That also cancels the
  // centring that GotoLine() has still to do.
  FileViewState GetViewState() const;
  void SetViewState(const FileViewState &state);
  // Shows the caret position and the size of the selection in the status bar
  // of the main window. Called when the caret or the selection moves (only
  // while this page is showing) and when the page is selected.
  void PublishStatus();

  // ---- Find and Replace ------------------------------------------------
  // Shows the search bar (find and replace) below the editor. The selected
  // text, if it is on one line, is what it looks for.
  void ShowFind();
  // Selects the next (or previous) match of the last search, with wrap around.
  // Shows the search bar if there was no search yet.
  void FindNext(bool forward);
  bool CanReplace() const { return m_editor->IsEditable(); }

private:
  // The search bar sends these to its parent, this page.
  void OnSearchBarFind(wxCommandEvent &event);
  void OnSearchBarReplace(wxCommandEvent &event);
  void OnSearchBarReplaceAll(wxCommandEvent &event);
  void OnSearchBarClose(wxCommandEvent &event);
  // Takes the text and the options of the search from an event of the bar.
  void TakeSearch(const wxCommandEvent &event);
  // The selected text, if it is on one line; empty otherwise.
  wxString SelectedSearchText() const;

  // Selects the next match from the selection, in the given direction, and
  // wraps around once. False if there is none. `incremental` (the text is
  // being typed) looks from the start of the selection: the match it has
  // stays if it still matches.
  bool FindIn(bool forward, bool incremental = false);
  int FindSearchFlags() const;
  void FileSaved(const Status &status);
  // Tells the views (wxEVT_FILE_SAVED) that the file was saved.
  void SendSaved();
  void OnSave(wxCommandEvent &event);
  void OnSaveUI(wxUpdateUIEvent &event);
  void OnClose(wxCommandEvent &event);
  void OnReload(wxCommandEvent &event);
  // The user toggled Wrap Lines on the toolbar of this page.
  void OnWrapToggled(wxCommandEvent &event);
  // The setting changed (on any page): this page follows it.
  void OnWrapLinesChanged(wxCommandEvent &event);

  // Puts the document back into the "has unsaved changes" state.
  void MarkModified();
  void OnKeyDown(wxKeyEvent &event);
  void OnSavePointChanged(wxStyledTextEvent &event);

  wxString m_path;
  RemoteHostDetails m_remoteHost;
  bool m_saving{false};
  // See LoadText().
  bool m_transient{false};
  // Counts the calls of GotoLine() and SetViewState(): the deferred centring of
  // a call that is not the last one is dropped.
  int m_viewSerial{0};
  // Joinable, and owned by the page so the page (the event sink) is alive for
  // as long as the thread runs: the destructor joins it.
  std::thread m_saveThread;
  Editor *m_editor{nullptr};
  wxAuiToolBar *m_toolbar{nullptr};
  SearchBar *m_searchBar{nullptr}; // Hidden until Find
  wxString m_findText;             // What the last search looked for
  int m_findOptions{0};            // Its SearchBar::kWholeWord / kMatchCase
  // Lets Ctrl+Tab (and the other global shortcuts) work while the
  // editor has focus.
  std::unique_ptr<AcceleratorInterceptor> m_acceleratorInterceptor;
};
