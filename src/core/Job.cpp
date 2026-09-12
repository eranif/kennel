#include "core/Job.h"

wxString JobTypeToString(JobType type) {
  switch (type) {
  case JobType::kPrompt:
    return "prompt";
  case JobType::kRawCommand:
  default:
    return "rawCommand";
  }
}

JobType JobTypeFromString(const wxString &str) {
  if (str == "prompt") {
    return JobType::kPrompt;
  }
  return JobType::kRawCommand;
}

wxString ScheduleModeToString(ScheduleMode mode) {
  switch (mode) {
  case ScheduleMode::kDailyAt:
    return "dailyAt";
  case ScheduleMode::kIntervalHours:
  default:
    return "intervalHours";
  }
}

ScheduleMode ScheduleModeFromString(const wxString &str) {
  if (str == "dailyAt") {
    return ScheduleMode::kDailyAt;
  }
  return ScheduleMode::kIntervalHours;
}
