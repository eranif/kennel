#include "app/KennelManager.hpp"

#include "app/FilePage.hpp"
#include "app/MainFrame.h"
#include "app/MainNotebook.hpp"
#include "app/MainView.hpp"

IEditor *KennelManager::GetActiveEditor() const {
  // No window while the application starts and ends.
  if (GetMainFrame() == nullptr) {
    return nullptr;
  }
  return GetMainView()->GetSessionsBook()->GetActiveFilePage();
}

std::vector<IEditor *> KennelManager::GetAllEditors() const {
  std::vector<IEditor *> editors;
  if (GetMainFrame() == nullptr) {
    return editors;
  }
  for (FilePage *page : GetMainView()->GetSessionsBook()->GetFiles()) {
    editors.push_back(page);
  }
  return editors;
}

IEditor *KennelManager::FindEditorByUID(FilePageUID uid) const {
  for (IEditor *editor : GetAllEditors()) {
    if (editor->GetEditorUID() == uid) {
      return editor;
    }
  }
  return nullptr;
}
