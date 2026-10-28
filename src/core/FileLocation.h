#pragma once

#include <wx/string.h>

// A place in a file, as compilers print it: "path", "path:line" or
// "path:line:column" (GCC, clang, ...). The line and the column start at 1; 0
// means that the text did not give one.
struct FileLocation {
  wxString path;
  int line{0};
  int column{0};
};

// Splits "path:line:column" (the column is optional) into its parts. The path
// can be a Windows path with a drive ("C:\a\b.cpp:10:5"), a relative path or a
// Linux path. Text that does not end with ":<number>" is all path, with line
// and column 0. A drive letter is never taken for a line: "C:12" has no line.
FileLocation ParseFileLocation(const wxString &text);
