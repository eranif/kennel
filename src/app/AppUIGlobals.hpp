#pragma once

#include <optional>
#include <wx/statusbr.h>
#include <wx/string.h>


void SetStatusText(const wxString &message, int field = 0,
                   wxStatusBar *statusBar = nullptr);

struct StatusBarLocker {
  std::optional<wxString> m_oldMessage{std::nullopt};
  wxStatusBar *m_statusBar{nullptr};

  int m_field{0};
  StatusBarLocker(wxStatusBar *statusBar = nullptr, int field = 0);
  ~StatusBarLocker();
};

