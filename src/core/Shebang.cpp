#include "core/Shebang.h"

#include <wx/tokenzr.h>
#include <wx/wxcrt.h>

Shebang::Shebang(const wxString &firstLine) {
  const wxString line = firstLine.Strip(wxString::both);
  if (line.StartsWith("<?php")) {
    m_interpreter = "php";
    return;
  }
  if (!line.StartsWith("#!")) {
    return;
  }

  // The program of the shebang. With `env` it is the first word after it that
  // is not an option or a variable ("#!/usr/bin/env -S python3 -u").
  wxStringTokenizer words(line.Mid(2), " \t", wxTOKEN_STRTOK);
  bool afterEnv = false;
  while (words.HasMoreTokens()) {
    const wxString word = words.GetNextToken();
    if (afterEnv && (word.StartsWith("-") || word.Contains("="))) {
      continue;
    }
    const wxString name = word.AfterLast('/').Lower();
    if (name == "env" && !afterEnv) {
      afterEnv = true;
      continue;
    }
    m_interpreter = name;
    break;
  }
  // "python3.11" and "ksh93" are python and ksh.
  while (!m_interpreter.empty() &&
         (wxIsdigit(m_interpreter.Last()) || m_interpreter.Last() == '.')) {
    m_interpreter.RemoveLast();
  }
}
