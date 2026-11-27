#pragma once

#include "core/IEditor.h"

#include <vector>

// What the core classes (the LspManager and its clients) can ask of the
// application, to reach what is live in it instead of keeping a copy: the
// editors that are open. The application implements it (KennelManager) and
// gives it to the core with SetKennelManager() (see Globals.hpp). GUI-free.
class IManager {
public:
  virtual ~IManager() = default;

  // The editor that the user works in, or null when the page that is showing is
  // not an editor.
  virtual IEditor *GetActiveEditor() const = 0;
  // Every editor that is open.
  virtual std::vector<IEditor *> GetAllEditors() const = 0;
  // The editor with this ID (see IEditor::GetEditorUID()), or null when it is
  // not open.
  virtual IEditor *FindEditorByUID(FilePageUID uid) const = 0;
};
