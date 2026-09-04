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
