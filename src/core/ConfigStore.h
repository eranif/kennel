#pragma once

#include <wx/string.h>

#include "core/AppPaths.h"
#include "core/Config.h"
#include "core/Status.h"

#include <vector>

// Loads and saves config.json (global settings + adapter definitions).
// Validation is tolerant: structurally valid JSON loads even with missing
// fields (defaults fill in), and individual adapters with bad regex patterns
// are kept (the offending patterns are dropped and reported in Warnings()).
// Malformed JSON is a hard error surfaced via Status/StatusOr; nothing throws.
class ConfigStore {
public:
  explicit ConfigStore(AppPaths paths);

  // Loads config.json. If the file is absent, writes DefaultConfig() to disk
  // and returns it. Returns an error status on malformed JSON or I/O failure.
  StatusOr<AppConfig> Load();

  // Serializes and writes the config to config.json (pretty-printed).
  Status Save(const AppConfig &);

  // Raw file text for the in-app editor. An absent file yields an empty
  // string (OK); an unreadable file yields an error status.
  StatusOr<wxString> RawText();

  // Validates that text is well-formed JSON, then writes it verbatim. On
  // invalid JSON returns an error status and leaves the existing file
  // untouched.
  Status SaveRawText(const wxString &text);

  // Non-fatal issues from the most recent Load (e.g. skipped regex patterns).
  const std::vector<wxString> &Warnings() const { return m_warnings; }

private:
  AppPaths m_paths;
  std::vector<wxString> m_warnings;
};
