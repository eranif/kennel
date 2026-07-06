#pragma once

#include "core/AppPaths.h"
#include "core/Status.h"
#include "core/UiPrefs.h"

// Loads and saves .persist.json (UI preferences). Tolerant by design: a
// missing or unreadable/corrupt file yields default UiPrefs rather than an
// error, since these settings are non-essential and safely deletable. Only a
// write failure is surfaced as an error. Nothing throws.
class UiPrefsStore {
public:
  explicit UiPrefsStore(AppPaths paths);

  // Load prefs
  Status Load(UiPrefs &prefs);

  // Serializes and writes preferences to .persist.json (pretty-printed).
  Status Save(const UiPrefs &);

private:
  AppPaths m_paths;
};
