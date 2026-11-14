#include "core/ZipArchive.h"

#include <algorithm>
#include <memory>
#include <wx/mstream.h>
#include <wx/zipstrm.h>

StatusOr<std::string> ReadZipMember(const std::string &archive,
                                    const wxString &name) {
  wxMemoryInputStream raw(archive.data(), archive.size());
  wxZipInputStream zip(raw);
  if (!zip.IsOk()) {
    return Status::Error(_("The archive is not a valid .zip file"));
  }

  const wxString suffix = "/" + name;
  for (std::unique_ptr<wxZipEntry> entry(zip.GetNextEntry()); entry;
       entry.reset(zip.GetNextEntry())) {
    if (entry->IsDir()) {
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
    while (zip.IsOk() && !zip.Eof()) {
      zip.Read(buffer, sizeof(buffer));
      const size_t count = zip.LastRead();
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
