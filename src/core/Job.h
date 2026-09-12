#pragma once

#include <wx/string.h>

// One timer-based job definition (config.json -> "jobs[]"). A job either runs
// a raw shell command, or sends a prompt to a configured agent's
// non-interactive mode. It fires either on a fixed cadence measured in hours,
// or once a day at a fixed time (see ScheduleMode and JobScheduler).
enum class JobType { kRawCommand, kPrompt };

// kIntervalHours: fires every intervalHours hours.
// kDailyAt: fires once a day at dailyHour:dailyMinute (24h clock).
enum class ScheduleMode { kIntervalHours, kDailyAt };

struct JobDef {
  wxString name;
  JobType type = JobType::kRawCommand;
  wxString command;   // shell command to run, when type == kRawCommand
  wxString agentName; // references AgentDef::name, when type == kPrompt
  wxString prompt;    // prompt text to send, when type == kPrompt
  ScheduleMode scheduleMode = ScheduleMode::kIntervalHours;
  int intervalHours = 1; // used when scheduleMode == kIntervalHours
  int dailyHour = 10;    // used when scheduleMode == kDailyAt, 0-23
  int dailyMinute = 0;   // used when scheduleMode == kDailyAt, 0-59
  bool keepTerminalOpen = true;
  bool enabled = true; // A disabled job is never triggered by the scheduler.
};

wxString JobTypeToString(JobType type);
JobType JobTypeFromString(const wxString &str);

wxString ScheduleModeToString(ScheduleMode mode);
ScheduleMode ScheduleModeFromString(const wxString &str);
