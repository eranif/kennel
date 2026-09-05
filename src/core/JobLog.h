#pragma once

#include <wx/string.h>

#include <vector>

// One row appended to <LogsDir>/jobs.log, written as a single JSON object per
// line (JSON Lines / NDJSON — not a JSON array), so the file stays
// append-only and is never re-read+rewritten just to add an entry.
//
// `event` is one of "start", "end", "failed". Fields that don't apply to a
// given event are left empty and omitted from the JSON (e.g. `reason` only
// makes sense for "failed"). Best-effort: a logging failure is silently
// ignored and never blocks or fails a job run.
struct JobLogEntry {
  wxString event;
  wxString job;
  wxString type;    // "rawCommand" | "prompt" (start/failed only)
  wxString trigger; // "manual" | "scheduled" (start/failed only)
  wxString session; // start/end only
  wxString reason;  // failed only
  wxString message; // human-readable one-line summary
};

void AppendJobLogEntry(const JobLogEntry &entry);

// A JobLogEntry read back from disk, plus its recorded timestamp.
struct JobLogRecord : JobLogEntry {
  wxString timestamp;
};

// Reads and parses every line of jobs.log, oldest first. Lines that fail to
// parse (corrupt/partial) are skipped rather than failing the whole read. An
// absent file yields an empty list.
std::vector<JobLogRecord> ReadJobLog();
