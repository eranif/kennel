#pragma once

#include <wx/string.h>

// One timer-based job definition (config.json -> "jobs[]"). A job either runs
// a raw shell command, or sends a prompt to a configured agent's
// non-interactive mode. It fires on a fixed cadence measured in hours since
// Kennel started (see JobScheduler).
enum class JobType { kRawCommand, kPrompt };

struct JobDef {
  wxString name;
  JobType type = JobType::kRawCommand;
  wxString command;   // shell command to run, when type == kRawCommand
  wxString agentName; // references AgentDef::name, when type == kPrompt
  wxString prompt;    // prompt text to send, when type == kPrompt
  int intervalHours = 1;
  bool keepTerminalOpen = true;
  bool enabled = true; // A disabled job is never triggered by the scheduler.
};

wxString JobTypeToString(JobType type);
JobType JobTypeFromString(const wxString &str);
