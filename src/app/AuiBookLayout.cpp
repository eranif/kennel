#include "app/AuiBookLayout.hpp"

#include "core/JsonUtil.h"
#include "core/json.hpp"

#include <algorithm>

using nlohmann::json;

void AuiBookLayout::BeforeSaveNotebook(const wxString &) { m_tabs.clear(); }

void AuiBookLayout::SaveNotebookTabControl(const wxAuiTabLayoutInfo &tab) {
  m_tabs.push_back(tab);
}

std::vector<wxAuiTabLayoutInfo>
AuiBookLayout::LoadNotebookTabs(const wxString &) {
  return m_tabs;
}

wxString AuiBookLayout::ToString() const {
  if (m_tabs.empty()) {
    return wxString{};
  }
  json tabs = json::array();
  for (const wxAuiTabLayoutInfo &tab : m_tabs) {
    tabs.push_back({{"direction", tab.dock_direction},
                    {"layer", tab.dock_layer},
                    {"row", tab.dock_row},
                    {"pos", tab.dock_pos},
                    {"proportion", tab.dock_proportion},
                    {"size", tab.dock_size},
                    {"pages", tab.pages},
                    {"pinned", tab.pinned},
                    {"active", tab.active}});
  }
  return wxString::FromUTF8(tabs.dump());
}

bool AuiBookLayout::FromString(const wxString &text) {
  m_tabs.clear();
  if (text.empty()) {
    return false;
  }
  const json tabs = json::parse(text.ToStdString(wxConvUTF8), nullptr,
                                /*allow_exceptions=*/false);
  if (!tabs.is_array() || tabs.empty()) {
    return false;
  }
  for (const json &item : tabs) {
    if (!item.is_object()) {
      m_tabs.clear();
      return false;
    }
    wxAuiTabLayoutInfo tab;
    tab.dock_direction =
        jsonutil::GetInt(item, "direction", tab.dock_direction);
    tab.dock_layer = jsonutil::GetInt(item, "layer", tab.dock_layer);
    tab.dock_row = jsonutil::GetInt(item, "row", tab.dock_row);
    tab.dock_pos = jsonutil::GetInt(item, "pos", tab.dock_pos);
    tab.dock_proportion =
        jsonutil::GetInt(item, "proportion", tab.dock_proportion);
    tab.dock_size = jsonutil::GetInt(item, "size", tab.dock_size);
    tab.active = jsonutil::GetInt(item, "active", tab.active);
    // The lists of pages: a value that is not a number makes the layout bad.
    for (const auto &[key, list] :
         {std::pair{"pages", &tab.pages}, std::pair{"pinned", &tab.pinned}}) {
      const auto found = item.find(key);
      if (found == item.end()) {
        continue;
      }
      if (!found->is_array()) {
        m_tabs.clear();
        return false;
      }
      for (const json &number : *found) {
        if (!number.is_number_integer()) {
          m_tabs.clear();
          return false;
        }
        list->push_back(number.get<int>());
      }
    }
    m_tabs.push_back(std::move(tab));
  }
  return true;
}

bool AuiBookLayout::FitsPages(size_t pageCount) const {
  if (m_tabs.empty()) {
    return false;
  }
  // One tab control with no list of pages holds all of them, in order.
  if (m_tabs.size() == 1 && m_tabs.front().pages.empty()) {
    return true;
  }
  std::vector<bool> seen(pageCount, false);
  size_t count = 0;
  for (const wxAuiTabLayoutInfo &tab : m_tabs) {
    for (const int page : tab.pages) {
      if (page < 0 || static_cast<size_t>(page) >= pageCount || seen[page]) {
        return false;
      }
      seen[page] = true;
      ++count;
    }
  }
  return count == pageCount;
}
