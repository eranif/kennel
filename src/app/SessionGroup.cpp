#include "app/SessionGroup.h"

SessionGroup::SessionGroup(const wxString &groupName, bool terminalsGroup,
                           bool filesGroup)
    : m_groupName{groupName}, m_terminalsGroup{terminalsGroup},
      m_filesGroup{filesGroup} {}
