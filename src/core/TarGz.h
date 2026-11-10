#pragma once

#include "core/Status.h"

#include <string>
#include <wx/string.h>

// Reads one file out of a .tar.gz held in memory. `name` is the last part of
// the path of the member ("rg" finds "ripgrep-15.2.0-x/rg", but not
// "ripgrep-15.2.0-x/doc/rg.1"). The first member that matches wins. GUI-free
// (wxBase streams only).
StatusOr<std::string> ReadTarGzMember(const std::string &archive,
                                      const wxString &name);
