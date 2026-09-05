#ifndef JOBLOGVIEWER_HPP
#define JOBLOGVIEWER_HPP
#include "UI.hpp"
#include "core/JobLog.h"

#include <vector>

class JobLogViewer : public JobLogViewerBase {
public:
  JobLogViewer(wxWindow *parent);
  ~JobLogViewer() override;

protected:
  void OnKeyDown(wxKeyEvent &event) override;
  void OnFilterUpdated(wxCommandEvent &event) override;
  void OnLogEntryActivated(wxDataViewEvent &event) override;

private:
  // Repopulates the list from m_entries, keeping only rows matching `filter`
  // (case-insensitive substring, empty = show all).
  void PopulateList(const wxString &filter);

  std::vector<JobLogRecord> m_entries; // full, unfiltered, newest first
};
#endif // JOBLOGVIEWER_HPP
