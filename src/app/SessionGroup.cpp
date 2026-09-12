#include "app/SessionGroup.h"

SessionGroup::SessionGroup(const wxString &groupName, bool terminalsGroup)
    : m_groupName{groupName}, m_terminalsGroup{terminalsGroup} {}
