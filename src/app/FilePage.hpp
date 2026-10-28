#pragma once

#include "app/AcceleratorInterceptor.h"
#include "app/Editor.h"
#include "app/FileEvent.hpp"
#include "terminal_theme.h"

#include <memory>
#include <thread>
#include <wx/aui/auibar.h>
#include <wx/panel.h>

class SearchBar;

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
  // The folder of the file: a path for a local file, "host:path" for a remote
  // one.
  wxString GetFolder() const;
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
  // Puts the caret at `line` and `column` (both from 1; the column is a byte
  // offset in the line, as compilers count it) and shows that line in the
  // middle of the editor. A line or a column out of range goes to the nearest
  // place that exists.
  void GotoLine(int line, int column = 1);
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
  void OnEditorUpdateUI(wxStyledTextEvent &event);

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
  void OnSave(wxCommandEvent &event);
  void OnSaveUI(wxUpdateUIEvent &event);
  void OnClose(wxCommandEvent &event);
  void OnReload(wxCommandEvent &event);

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
  wxAuiToolBar *m_toolbar{nullptr};
  SearchBar *m_searchBar{nullptr}; // Hidden until Find
  wxString m_findText;             // What the last search looked for
  int m_findOptions{0};            // Its SearchBar::kWholeWord / kMatchCase
  // Lets Ctrl+Tab (and the other global shortcuts) work while the
  // editor has focus.
  std::unique_ptr<AcceleratorInterceptor> m_acceleratorInterceptor;
};
