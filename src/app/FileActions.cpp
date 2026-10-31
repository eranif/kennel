#include "app/FileActions.h"

#include "app/MainNotebook.hpp"

#include <wx/filedlg.h>
#include <wx/intl.h>

namespace fileactions {

void OpenFiles(wxWindow *parent, MainNotebook *book) {
  wxFileDialog dialog(parent, _("Open File"), wxEmptyString, wxEmptyString,
                      _("All files (*.*)|*.*"),
                      wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);
  if (dialog.ShowModal() != wxID_OK) {
    return;
  }
  wxArrayString paths;
  dialog.GetPaths(paths);
  for (const wxString &path : paths) {
    book->OpenLocalFile(path);
  }
}

} // namespace fileactions
