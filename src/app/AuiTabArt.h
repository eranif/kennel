#pragma once

#include <wx/aui/aui.h>
#include <wx/aui/auibook.h>
#include <wx/aui/tabart.h>
#include <wx/colour.h>
#include <wx/dc.h>
#include <wx/version.h>

// This tab art provider draws flat tabs with a thin border.
class AuiFlatTabArt : public wxAuiTabArtBase {
public:
  AuiFlatTabArt();
  virtual ~AuiFlatTabArt();

  // Objects of this class are supposed to be used polymorphically, so
  // copying them is not allowed, use Clone() instead.
  AuiFlatTabArt(const AuiFlatTabArt &) = delete;
  AuiFlatTabArt &operator=(const AuiFlatTabArt &) = delete;

  wxAuiTabArt *Clone() override;

  void SetColour(const wxColour &colour) override;
  void SetActiveColour(const wxColour &colour) override;
  void DrawBackground(wxDC &dc, wxWindow *wnd, const wxRect &rect) override;
  int DrawPageTab(wxDC &dc, wxWindow *wnd, wxAuiNotebookPage &page,
                  const wxRect &rect) override;
  int GetIndentSize() override;

  wxSize GetPageTabSize(wxReadOnlyDC &dc, wxWindow *wnd,
                        const wxAuiNotebookPage &page,
                        int *xExtent = nullptr) override;
  int GetBestTabCtrlSize(wxWindow *wnd, const wxAuiNotebookPageArray &pages,
                         const wxSize &requiredBmpSize) override;
  void UpdateColoursFromSystem() override;

  void DrawBorder(wxDC &dc, wxWindow *wnd, const wxRect &rect) override;

private:
  // Private pseudo-copy ctor used by Clone().
  explicit AuiFlatTabArt(AuiFlatTabArt *other);

  virtual wxColour GetButtonColour(wxAuiButtonId button,
                                   wxAuiPaneButtonState state) const override;
  void DoDrawBackground(wxDC &dc, wxWindow *wnd, const wxRect &rect,
                        bool with_bg);

  struct Data;
  Data *const m_data{nullptr};
};
