#pragma once

class MainNotebook;
class wxWindow;

// The "Open File" command (File menu, Ctrl+O): asks for the files and opens
// them in editor pages of `book`. Local files only.
namespace fileactions {

// Asks for one or more files and opens them.
void OpenFiles(wxWindow *parent, MainNotebook *book);

} // namespace fileactions
