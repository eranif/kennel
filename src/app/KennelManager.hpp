#pragma once

#include "core/IManager.h"

// The IManager of the application: the editors are the file pages of the main
// notebook.
class KennelManager : public IManager {
public:
  IEditor *GetActiveEditor() const override;
  std::vector<IEditor *> GetAllEditors() const override;
  IEditor *FindEditorByUID(FilePageUID uid) const override;
};
