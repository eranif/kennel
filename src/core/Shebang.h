#pragma once

#include <wx/string.h>

// What the first line of a text file says about its type: a shebang
// ("#!/usr/bin/env python3") names the interpreter, and "<?php" is PHP. For
// the files that have no extension ("codelite-remote").
class Shebang {
public:
  // `firstLine` is the first line of the file (or the start of it).
  explicit Shebang(const wxString &firstLine);

  // The name of the interpreter, in lower case and with no version: "python",
  // "bash", "node", "php"... Empty when the line says nothing.
  const wxString &GetInterpreter() const { return m_interpreter; }

private:
  wxString m_interpreter;
};
