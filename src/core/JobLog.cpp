#include "core/JobLog.h"

#include "core/AppManager.h"
#include "core/JsonUtil.h"
#include "core/json.hpp"

#include <wx/datetime.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/tokenzr.h>

namespace {
using jsonutil::GetStr;
using jsonutil::ToUtf8;
using nlohmann::json;

wxFileName JobLogFile() {
  wxFileName logFile = AppManager::Get().Paths().LogsDir();
  logFile.SetFullName("jobs.log");
  return logFile;
}

json ToJson(const JobLogEntry &entry, const wxString &timestamp) {
  json j;
  j["timestamp"] = ToUtf8(timestamp);
  j["event"] = ToUtf8(entry.event);
  if (!entry.job.empty()) {
    j["job"] = ToUtf8(entry.job);
  }
  if (!entry.type.empty()) {
    j["type"] = ToUtf8(entry.type);
  }
  if (!entry.trigger.empty()) {
    j["trigger"] = ToUtf8(entry.trigger);
  }
  if (!entry.session.empty()) {
    j["session"] = ToUtf8(entry.session);
  }
  if (!entry.reason.empty()) {
    j["reason"] = ToUtf8(entry.reason);
  }
  if (!entry.message.empty()) {
    j["message"] = ToUtf8(entry.message);
  }
  return j;
}
} // namespace

void AppendJobLogEntry(const JobLogEntry &entry) {
  const json j = ToJson(entry, wxDateTime::Now().FormatISOCombined(' '));

  wxFFile file(JobLogFile().GetFullPath(), "a");
  if (!file.IsOpened()) {
    return;
  }
  file.Write(wxString::FromUTF8(j.dump()) + "\n");
}

std::vector<JobLogRecord> ReadJobLog() {
  std::vector<JobLogRecord> records;

  const wxString path = JobLogFile().GetFullPath();
  if (!wxFileExists(path)) {
    return records;
  }

  wxFFile file(path, "r");
  if (!file.IsOpened()) {
    return records;
  }
  wxString contents;
  if (!file.ReadAll(&contents, wxConvUTF8)) {
    return records;
  }

  wxStringTokenizer lines(contents, "\n");
  while (lines.HasMoreTokens()) {
    const std::string raw = ToUtf8(lines.GetNextToken());
    json j = json::parse(raw, /*cb=*/nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded() || !j.is_object()) {
      continue; // skip corrupt/partial lines rather than fail the whole read
    }

    JobLogRecord record;
    record.timestamp = GetStr(j, "timestamp");
    record.event = GetStr(j, "event");
    record.job = GetStr(j, "job");
    record.type = GetStr(j, "type");
    record.trigger = GetStr(j, "trigger");
    record.session = GetStr(j, "session");
    record.reason = GetStr(j, "reason");
    record.message = GetStr(j, "message");
    records.push_back(std::move(record));
  }
  return records;
}

wxString ToPrettyJson(const JobLogRecord &record) {
  const json j = ToJson(record, record.timestamp);
  return wxString::FromUTF8(j.dump(2));
}
