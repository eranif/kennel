#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>
#include <wx/bitmap.h>
#include <wx/window.h>

struct Tab {
  wxString title;
  wxString agentName;

  inline bool operator==(const Tab &other) const {
    return other.title == title;
  }
};

class TabHistory {
  std::vector<Tab> m_history;

public:
  TabHistory() = default;
  virtual ~TabHistory() = default;

  /// Compact the history, keeping only windows from the `windows` list
  /// If `add_missing` is true, we update the history with windows that
  /// exists in `windows` but not in this history object
  void Compact(const std::vector<Tab> &windows, bool add_missing);

  void Push(Tab page) {
    auto old = Pop(page);
    if (old && page.agentName.empty()) {
      page.agentName = old->agentName;
    }
    m_history.insert(m_history.begin(), page);
  }

  std::optional<Tab> Pop(Tab page) {
    auto iter = std::find_if(m_history.begin(), m_history.end(),
                             [&](Tab w) { return w == page; });
    if (iter == m_history.end()) {
      return std::nullopt;
    }
    Tab t = *iter;
    m_history.erase(iter);
    return t;
  }

  std::optional<Tab> PrevPage() const {
    if (m_history.empty()) {
      return std::nullopt;
    }
    // return the top of the heap
    return m_history[0];
  }

  /**
   * @brief clear the history
   */
  void Clear() { m_history.clear(); }

  /**
   * @brief return the tabbing history
   * @return
   */
  const std::vector<Tab> &GetHistory() const { return m_history; }
};
