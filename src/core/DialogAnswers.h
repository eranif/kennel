#pragma once

#include "core/AppPaths.h"
#include "core/Status.h"

#include <optional>
#include <vector>
#include <wx/string.h>

// The answers to the questions that Kennel asks "once": a question that the
// user answered and does not want to be asked again. Kept in
// dialog-answers.json, each with the question in words (the file is for people
// to read, and the Settings page shows the same text) so that the answer can be
// taken back there. GUI-free.
class DialogAnswers {
public:
  struct Entry {
    // Tells the dialog apart from the others, for the code: "lsp.install|...".
    wxString m_key;
    // The question as the user read it: "Install clangd on Local File System?".
    wxString m_question;
    // The answer: true for Yes, false for No.
    bool m_yes{false};
  };

  explicit DialogAnswers(AppPaths paths);

  // Loads dialog-answers.json. A missing file is no answers. A malformed one is
  // no answers too, and the file is left as it is until the next change.
  Status Load();

  // The answer stored for `key`, or nothing when the user is to be asked.
  std::optional<bool> FindAnswer(const wxString &key) const;

  // Stores (and saves) the answer: the next time the question is not asked.
  Status Store(const wxString &key, const wxString &question, bool yes);

  // Takes an answer back (and saves): the question is asked again.
  Status Remove(const wxString &key);
  Status RemoveAll();

  const std::vector<Entry> &GetEntries() const { return m_entries; }

private:
  Status Save() const;

  AppPaths m_paths;
  std::vector<Entry> m_entries;
};
