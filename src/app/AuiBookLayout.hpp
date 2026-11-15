#pragma once

#include <vector>
#include <wx/aui/serializer.h>
#include <wx/string.h>

// The layout of a wxAuiNotebook as a string: the order of its tabs, the tab
// controls that the user split off (where each is docked, and how big), and the
// tab that shows in each. The notebook writes it with SaveLayout() and reads it
// with LoadLayout(); this class is what they talk to, and turns it into text
// that can be kept in the preferences.
//
//   AuiBookLayout layout;
//   book->SaveLayout("name", layout);
//   prefs.layout = layout.ToString();
//   ...
//   AuiBookLayout layout;
//   if (layout.FromString(prefs.layout) &&
//   layout.FitsPages(book->GetPageCount())) {
//     book->LoadLayout("name", layout);
//   }
class AuiBookLayout : public wxAuiBookSerializer, public wxAuiBookDeserializer {
public:
  // The layout as text (JSON); empty when nothing was saved.
  wxString ToString() const;
  // Reads the text that ToString() made. False (and no layout) when it is not
  // one, for example when it is empty or damaged.
  bool FromString(const wxString &text);
  // Whether the layout is for a notebook with `pageCount` pages: each page is
  // in one tab control, and no other page is. A layout that does not fit is not
  // to be loaded (the notebook was changed since).
  bool FitsPages(size_t pageCount) const;

private:
  // wxAuiBookSerializer
  void BeforeSaveNotebook(const wxString &name) override;
  void SaveNotebookTabControl(const wxAuiTabLayoutInfo &tab) override;
  // wxAuiBookDeserializer
  std::vector<wxAuiTabLayoutInfo>
  LoadNotebookTabs(const wxString &name) override;

  std::vector<wxAuiTabLayoutInfo> m_tabs;
};
