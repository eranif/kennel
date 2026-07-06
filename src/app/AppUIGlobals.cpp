#include "app/AppUIGlobals.hpp"
#include "app/MainFrame.h"
#include "core/Helpers.h"
#include <wx/app.h>

void SetStatusText(const wxString &text, int field, wxStatusBar *statusBar) {
  if (statusBar == nullptr) {
    auto *mainFrame = dynamic_cast<MainFrame *>(wxTheApp->GetTopWindow());
    CHECK_NOT_NULL_RETURN(mainFrame);

    statusBar = mainFrame->GetStatusBar();
    CHECK_NOT_NULL_RETURN(statusBar);
  }

  statusBar->SetStatusText(text, field);
#ifndef __WXMAC__
  wxTheApp->SafeYield(wxTheApp->GetTopWindow(), true);
#endif
}

StatusBarLocker::StatusBarLocker(wxStatusBar *statusBar, int field)
    : m_statusBar{statusBar}, m_field{field} {
  if (m_statusBar == nullptr) {
    auto *mainFrame = dynamic_cast<MainFrame *>(wxTheApp->GetTopWindow());
    CHECK_NOT_NULL_RETURN(mainFrame);

    m_statusBar = mainFrame->GetStatusBar();
    CHECK_NOT_NULL_RETURN(m_statusBar);
  }

  if (field >= m_statusBar->GetFieldsCount()) {
    return;
  }

  m_oldMessage = m_statusBar->GetStatusText(m_field);
}

StatusBarLocker::~StatusBarLocker() {
  if (m_statusBar != nullptr && m_oldMessage.has_value()) {
    SetStatusText(*m_oldMessage, m_field, m_statusBar);
  }
}
