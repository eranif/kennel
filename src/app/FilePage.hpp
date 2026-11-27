#pragma once

#include "app/AcceleratorInterceptor.h"
#include "app/Editor.h"
#include "app/EditorState.hpp"
#include "app/FileEvent.hpp"
#include "app/FileViewState.hpp"
#include "app/SearchablePage.hpp"
#include "core/Account.h"
#include "core/lsp/LspManager.h"
#include "terminal_theme.h"

#include <memory>
#include <optional>
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
class FilePage : public wxPanel, public SearchablePage {
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
  // A reload of a remote file that did not give a text (the read failed): the
  // state of the editor that was kept for it is dropped, not put back.
  void CancelReload();
  // Shows `text` read-only, as a text that is not a file (a git diff): the
  // page is not kept in the session, and Reload() does nothing. `path` of the
  // page only names it (its extension decides the language).
  void LoadText(const wxString &text);
  bool IsTransient() const { return m_transient; }
  // Shows an empty text that is not a file yet ("Untitled"): the page is kept
  // out of the session, and Save asks for the file (see SaveAs()). The `m_path`
  // of the page holds the name that is shown, until the page is saved.
  void LoadUntitled();
  bool IsUntitled() const { return m_untitled; }

  wxString GetKey() const { return FileEvent::MakeKey(m_path, m_remoteHost); }
  const wxString &GetPath() const { return m_path; }
  const RemoteHostDetails &GetRemoteHostDetails() const { return m_remoteHost; }
  // The folder and the name: "c/file.cpp" for a/b/c/file.cpp.
  wxString GetDisplayName() const;
  // The text of the tab of the page: the name of the file, or the folder and
  // the name (GetDisplayName()) when the preferences say so
  // (UiPrefs::fileTabShowsFolder).
  wxString GetTabLabel() const;
  // The folder of the file: a path for a local file, "host:path" for a remote
  // one.
  wxString GetFolder() const;
  // The folder part of GetAccountPath(), in the notation of GetAccount()
  // ("C:\a\b" for "C:\a\b\file.cpp", "/home/me" for a remote file or a file of
  // WSL).
  wxString GetDirectory() const;
  // Whose file it is: a remote host, a WSL distribution (a file on the share
  // of the distro) or this machine.
  Account GetAccount() const;
  // The path in the notation of GetAccount(): a Linux path for a WSL file, the
  // path of the page otherwise. It is what a FileTreeCtrl of the account shows.
  wxString GetAccountPath() const;
  bool IsRemote() const { return !m_remoteHost.host.empty(); }
  // The key of the language server that the file needs, or nothing when it has
  // none, is not a file (a diff, an untitled page), or no open session holds
  // it.
  std::optional<LspServerKey> GetLanguageServerKey() const;
  bool IsModified() const { return m_editor->GetCtrl()->GetModify(); }
  bool CanSave() const {
    if (m_untitled) {
      return true; // It can always go to a new file
    }
    return IsRemote() ? m_editor->IsEditable() : m_editor->CanSave();
  }
  // Writes a local file back to disk; reports failure with a message box. An
  // untitled page asks for the file first (false if the user cancels).
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
  // place that exists. With a `length` (more than 0) the text from the column,
  // that many characters, is selected, and the column and the length count
  // characters, not bytes (what a search reports).
  void GotoLine(int line, int column = 1, int length = 0);
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
  // SearchablePage. The search bar (find and replace) is below the editor; the
  // search wraps around.
  bool CanFind() const override { return true; }
  void ShowFind() override;
  bool CanFindNext() const override { return true; }
  // With text selected this looks for that text: it goes in the search bar,
  // which is shown if it is hidden, and the match is the next (or previous) one
  // from the selection.
  void FindNext(bool forward) override;
  wxString GetFindWhat() const override;
  SearchTarget GetSearchTarget() const override;
  bool CanReplace() const { return m_editor->IsEditable(); }

  // ---- Bookmarks (the Bookmarks menu of Edit and the toolbar) -----------
  // On the line of the caret.
  void ToggleBookmark();
  // From the line of the caret, wrapping around; says so when there is none.
  void GotoBookmark(bool forward);
  void ClearBookmarks();

private:
  void InitDone();

  // The client of the language server of the file, or null when it has none or
  // it is not ready. With `create` the LspManager starts the server if it is
  // not known (LspManager::GetServerFor(), and the reason is logged),
  // without it only a server that exists is given
  // (LspManager::GetServerIfExistsFor()).
  std::shared_ptr<LspClient> GetServer(bool create) const;
  // Asks the LspManager for the language server of the file (see
  // LspManager::GetServerFor()). Nothing for a file that has no language
  // server, or that no open session holds.
  void RequestLanguageServer();
  // A language server is ready: when it is the one of this file, asks again.
  void OnLspServerReady(LspManagerEvent &event);
  // The tools of the bookmarks and of the editor options on the toolbar.
  void OnBookmarkTool(wxCommandEvent &event);
  // The user toggled Show Line Numbers or Highlight Word on the toolbar.
  void OnEditorOptionToggled(wxCommandEvent &event);
  // An editor option changed (on any page, or in the Edit menu): the tools of
  // the line numbers and of the highlighted word follow it.
  void OnEditorOptionsChanged(wxCommandEvent &event);
  // The search bar sends these to its parent, this page.
  void OnSearchBarFind(wxCommandEvent &event);
  void OnSearchBarReplace(wxCommandEvent &event);
  void OnSearchBarReplaceAll(wxCommandEvent &event);
  void OnSearchBarClose(wxCommandEvent &event);
  // Takes the text and the options of the search from an event of the bar.
  void TakeSearch(const wxCommandEvent &event);

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
  void OnReload(wxCommandEvent &event);
  // The user toggled Wrap Lines on the toolbar of this page.
  void OnWrapToggled(wxCommandEvent &event);
  // The setting changed (on any page): this page follows it.
  void OnWrapLinesChanged(wxCommandEvent &event);

  // Puts the document back into the "has unsaved changes" state.
  void MarkModified();
  void OnKeyDown(wxKeyEvent &event);
  void OnSavePointChanged(wxStyledTextEvent &event);
  // Asks for a file, saves the text there, and the page is that file from now
  // on (wxEVT_FILE_PATH_CHANGED). False if the user cancelled or it failed.
  bool SaveAs();

  wxString m_path;
  RemoteHostDetails m_remoteHost;
  bool m_saving{false};
  // See LoadText().
  bool m_transient{false};
  // See LoadUntitled().
  bool m_untitled{false};
  // Counts the calls of GotoLine() and SetViewState(): the deferred centring of
  // a call that is not the last one is dropped.
  int m_viewSerial{0};
  // Joinable, and owned by the page so the page (the event sink) is alive for
  // as long as the thread runs: the destructor joins it.
  std::thread m_saveThread;
  Editor *m_editor{nullptr};
  // The state of the editor before a reload of a remote file, until the text
  // arrives (LoadRemote()) and it is put back.
  std::unique_ptr<EditorState> m_reloadState;
  wxAuiToolBar *m_toolbar{nullptr};
  SearchBar *m_searchBar{nullptr}; // Hidden until Find
  wxString m_findText;             // What the last search looked for
  int m_findOptions{0};            // Its SearchBar::kWholeWord / kMatchCase
  // Lets Ctrl+Tab (and the other global shortcuts) work while the
  // editor has focus.
  std::unique_ptr<AcceleratorInterceptor> m_acceleratorInterceptor;
};
