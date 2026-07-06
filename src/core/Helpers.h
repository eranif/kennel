#pragma once

#include "core/Process.hpp"
#include "core/Status.h"
#include <chrono>
#include <future>
#include <wx/app.h>
#include <wx/arrstr.h>
#include <wx/dialog.h>
#include <wx/tokenzr.h>

#define __CHECK_COND_RETURN_VALUE(Cond, RetVal)                                \
  if (!(Cond)) {                                                               \
    return RetVal;                                                             \
  }

#define __CHECK_ITEM_RETURN_VALUE(Item, RetVal)                                \
  __CHECK_COND_RETURN_VALUE(Item.IsOk(), RetVal)

#define CHECK_ITEM_RETURN(Item) __CHECK_ITEM_RETURN_VALUE(Item, )
#define CHECK_NOT_EMPTY_OR_RETURN(Obj) __CHECK_COND_RETURN_VALUE(!Obj.empty(), )
#define CHECK_NOT_NULL_RETURN(Ptr) __CHECK_COND_RETURN_VALUE(Ptr != nullptr, )

enum Orientation {
  kNone = 0,
  kTop = kNone,
  kDefault = kNone,
  kResize = 1 << 1,
  kResizeSmall = 1 << 1,
};

inline void PositionDialog(wxWindow *window,
                           size_t flags = Orientation::kDefault) {
  CHECK_NOT_NULL_RETURN(window);
  auto parent = window->GetParent();
  if (parent == nullptr) {
    parent = wxTheApp->GetTopWindow();
  }
  CHECK_NOT_NULL_RETURN(parent);

  if (flags & (kResize | kResizeSmall)) {
    wxSize size = parent->GetSize();
    if (flags & kResize) {
      // 2 / 3 of the paren't size
      size.SetWidth((size.GetWidth() / 3) * 2);
      size.SetHeight((size.GetHeight() / 3) * 2);
    } else {
      // 1 / 3 of the paren't size
      size.SetWidth(size.GetWidth() / 3);
      size.SetHeight(size.GetHeight() / 3);
    }
    window->SetSize(size);
  }
  window->CenterOnParent();
}

inline StatusOr<std::string>
RunProcessWithTimeout(const std::vector<std::string> &command, int secs) {
  auto output = std::make_shared<std::string>();
  auto error = std::make_shared<std::string>();
  auto on_output_cb = [output, error](const std::string &out,
                                      const std::string &err) -> bool {
    output->append(out);
    error->append(err);
    return true;
  };

  auto output_promise = std::make_shared<std::promise<int>>();
  std::future<int> output_future = output_promise->get_future();
  auto on_end_cb = [output_promise](int exit_code) {
    output_promise->set_value(exit_code);
  };

  if (!Process::RunProcessAsync(command, std::move(on_output_cb),
                                std::move(on_end_cb))) {
    return Status::Error("Failed to launch process");
  }

  // Wait on the future for a maximum of secs seconds
  auto status = output_future.wait_for(std::chrono::seconds(secs));
  if (status == std::future_status::ready) {
    // The value is ready, retrieve it without blocking
    int exit_code = output_future.get();
    if (exit_code == 0) {
      // successfull execution
      std::string result;
      result = *output + "\n" + *error;
      return result;
    }
    return Status::Error(
        wxString::Format("Process exit with error code: %d", exit_code));
  } else if (status == std::future_status::timeout) {
    return Status::Error("Timed-out while waiting for process completion");
  } else {
    return Status::Error("Task deferred");
  }
};

inline wxString MakeAppTitle(const wxString &name,
                             const wxString &agentName = wxEmptyString) {
  wxString title = _("Kennel");
  if (!agentName.empty()) {
    title << wxT(" 🞂 ") << agentName;
  }
  if (!name.empty()) {
    title << wxT(" 🞂 ") << name;
  }
  title.Trim().Trim(false);
  return title;
}

inline wxString WrapWithDoubleQuotes(const wxString &str) {
  if (str.Contains(" ") && !str.StartsWith("\"") && !str.EndsWith("\"")) {
    return "\"" + str + "\"";
  }
  return str;
}

struct FindShellResult {
  std::vector<std::pair<wxString, wxString>> shells;
  int defaultShell{wxNOT_FOUND};
  wxString GetDefaultShellCmd() const { return shells[defaultShell].second; }
};
/// Collect the platform available shells.
FindShellResult FindShells();

/// Return list of all available shells.
wxArrayString FindShellNames();

/// Given a shell name, return its command
std::optional<wxString> FindShellCommand(const wxString &shellName);

/// Given a shell name, return its command
std::optional<wxString> FindShellNameByCommand(const wxString &shellCommand);

namespace platform {
std::optional<wxString> Which(const wxString &command,
                              bool useSystemPath = true);
}
