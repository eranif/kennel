#include "app/TabHistory.h"

#include <unordered_set>

void TabHistory::Compact(const std::vector<Tab> &windows, bool add_missing) {
  // convert the list into set
  std::unordered_set<Tab> windows_to_keep{windows.begin(), windows.end()};

  // Temporary list that holds the history that we want to keep
  std::vector<Tab> new_list;
  new_list.reserve(m_history.size());

  for (auto win : m_history) {
    if (windows_to_keep.count(win)) {
      new_list.push_back(win);
      windows_to_keep.erase(win);
    }
  }
  m_history.swap(new_list);

  if (add_missing && !windows_to_keep.empty()) {
    // some windows do not exist in the history? add them
    for (auto win : windows_to_keep) {
      m_history.push_back(win);
    }
  }
}
