#pragma once

#include <wx/intl.h>
#include <wx/string.h>

// The group a session is in when the user did not choose one.
inline const wxString kDefaultGroupName = _("Default");

// Names of the two built-in containers next to the user's own groups.
inline const wxString kTerminalsGroupName = _("Terminals");
inline const wxString kFilesGroupName = _("Files");

// The containers "Terminals" and "Files" are not groups: no session or group
// may use their names.
inline bool IsReservedGroupName(const wxString &name) {
  wxString trimmed = name;
  trimmed.Trim().Trim(false);
  return trimmed.IsSameAs(kTerminalsGroupName, false) ||
         trimmed.IsSameAs(kFilesGroupName, false);
}

// Whether `name` is a group of agent sessions (not one of the containers).
inline bool IsSessionGroupName(const wxString &name) {
  return name != kTerminalsGroupName && name != kFilesGroupName;
}
