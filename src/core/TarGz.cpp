#include "core/TarGz.h"

#include <algorithm>
#include <memory>
#include <wx/mstream.h>
#include <wx/sstream.h>
#include <wx/tarstrm.h>
#include <wx/zstream.h>

StatusOr<std::string> ReadTarGzMember(const std::string &archive,
                                      const wxString &name) {
  wxMemoryInputStream raw(archive.data(), archive.size());
  wxZlibInputStream gzip(raw, wxZLIB_GZIP);
  if (!gzip.IsOk()) {
    return Status::Error(_("The archive is not a valid .tar.gz file"));
  }

  wxTarInputStream tar(gzip);
  const wxString suffix = "/" + name;
  for (std::unique_ptr<wxTarEntry> entry(tar.GetNextEntry()); entry;
       entry.reset(tar.GetNextEntry())) {
    // Only a regular file has content: a link of the same name has none.
    if (entry->GetTypeFlag() != wxTAR_REGTYPE &&
        entry->GetTypeFlag() != wxTAR_CONTTYPE) {
      continue;
    }
    const wxString member = entry->GetName(wxPATH_UNIX);
    if (member != name && !member.EndsWith(suffix)) {
      continue;
    }

    std::string content;
    // The size is what the archive says: not trusted for the memory to take.
    content.reserve(std::min<size_t>(static_cast<size_t>(entry->GetSize()),
                                     64 * 1024 * 1024));
    char buffer[64 * 1024];
    while (tar.IsOk() && !tar.Eof()) {
      tar.Read(buffer, sizeof(buffer));
      const size_t count = tar.LastRead();
      if (count == 0) {
        break;
      }
      content.append(buffer, count);
    }
    if (content.size() != static_cast<size_t>(entry->GetSize())) {
      return Status::Error(wxString::Format(
          _("The archive is damaged (%s is cut short)"), name));
    }
    return content;
  }
  return Status::Error(
      wxString::Format(_("The archive does not contain %s"), name));
}
