#include "core/DialogAnswers.h"

#include "core/JsonUtil.h"
#include "core/Logger.h"

#include <algorithm>

using jsonutil::GetStr;
using jsonutil::ReadFileUtf8;
using jsonutil::ToUtf8;
using jsonutil::WriteFileUtf8;
using nlohmann::json;

DialogAnswers::DialogAnswers(AppPaths paths) : m_paths(std::move(paths)) {}

Status DialogAnswers::Load() {
  m_entries.clear();
  const wxString path = m_paths.DialogAnswersFile().GetFullPath();
  if (!wxFileExists(path)) {
    return Status::Ok();
  }

  std::string raw;
  if (!ReadFileUtf8(path, &raw)) {
    return Status::Error(
        wxString::Format("Could not read the dialog answers: %s", path));
  }
  const json root =
      json::parse(raw, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded() || !root.is_object()) {
    KLOG_WARN() << "dialog-answers.json is malformed; ignoring";
    return Status::Ok();
  }
  const auto answers = root.find("answers");
  if (answers == root.end() || !answers->is_array()) {
    return Status::Ok();
  }
  for (const json &item : *answers) {
    if (!item.is_object()) {
      continue;
    }
    Entry entry;
    entry.m_key = GetStr(item, "key");
    entry.m_question = GetStr(item, "question");
    entry.m_yes = GetStr(item, "answer") == "yes";
    if (!entry.m_key.empty()) {
      m_entries.push_back(std::move(entry));
    }
  }
  KLOG_DEBUG() << "Dialog answers: " << m_entries.size() << " stored";
  return Status::Ok();
}

std::optional<bool> DialogAnswers::FindAnswer(const wxString &key) const {
  const auto it =
      std::find_if(m_entries.begin(), m_entries.end(),
                   [&key](const Entry &entry) { return entry.m_key == key; });
  if (it == m_entries.end()) {
    return std::nullopt;
  }
  return it->m_yes;
}

Status DialogAnswers::Store(const wxString &key, const wxString &question,
                            bool yes) {
  const auto it =
      std::find_if(m_entries.begin(), m_entries.end(),
                   [&key](const Entry &entry) { return entry.m_key == key; });
  if (it != m_entries.end()) {
    it->m_question = question;
    it->m_yes = yes;
  } else {
    m_entries.push_back({key, question, yes});
  }
  KLOG_DEBUG() << "Dialog answers: stored " << (yes ? "yes" : "no") << " for "
               << key;
  return Save();
}

Status DialogAnswers::Remove(const wxString &key) {
  const auto it =
      std::remove_if(m_entries.begin(), m_entries.end(),
                     [&key](const Entry &entry) { return entry.m_key == key; });
  if (it == m_entries.end()) {
    return Status::Ok();
  }
  m_entries.erase(it, m_entries.end());
  KLOG_DEBUG() << "Dialog answers: removed " << key;
  return Save();
}

Status DialogAnswers::RemoveAll() {
  m_entries.clear();
  KLOG_DEBUG() << "Dialog answers: removed all";
  return Save();
}

Status DialogAnswers::Save() const {
  json answers = json::array();
  for (const Entry &entry : m_entries) {
    answers.push_back({{"key", ToUtf8(entry.m_key)},
                       {"question", ToUtf8(entry.m_question)},
                       {"answer", entry.m_yes ? "yes" : "no"}});
  }
  const json root = {{"version", 1}, {"answers", answers}};
  const wxString path = m_paths.DialogAnswersFile().GetFullPath();
  if (!WriteFileUtf8(path, root.dump(2))) {
    return Status::Error(
        wxString::Format("Could not write the dialog answers: %s", path));
  }
  return Status::Ok();
}
