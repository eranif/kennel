#ifndef KENNEL_WINDOWBOOK_HPP
#define KENNEL_WINDOWBOOK_HPP

#include <wx/bookctrl.h>
#include <wx/vector.h>
#include <wx/withimages.h>

// A page container without a user-visible controller. Its page-management API
// follows wxSimplebook, but it derives directly from wxControl.
class WindowBook : public wxControl {
public:
  WindowBook() { Init(); }

  WindowBook(wxWindow *parent, wxWindowID id = wxID_ANY,
             const wxPoint &pos = wxDefaultPosition,
             const wxSize &size = wxDefaultSize, long style = 0,
             const wxString &name = wxEmptyString) {
    Create(parent, id, pos, size, style, name);
  }

  bool Create(wxWindow *parent, wxWindowID id = wxID_ANY,
              const wxPoint &pos = wxDefaultPosition,
              const wxSize &size = wxDefaultSize, long style = 0,
              const wxString &name = wxEmptyString) {
    if (!wxControl::Create(parent, id, pos, size, style, wxDefaultValidator,
                           name)) {
      return false;
    }

    Init();
    Bind(wxEVT_SIZE, &WindowBook::OnSize, this);
    return true;
  }

  size_t GetPageCount() const { return m_pages.size(); }

  wxWindow *GetPage(size_t n) const {
    return n < GetPageCount() ? m_pages[n].window : nullptr;
  }

  wxWindow *GetCurrentPage() const {
    return m_selection == wxNOT_FOUND
               ? nullptr
               : GetPage(static_cast<size_t>(m_selection));
  }

  int GetSelection() const { return m_selection; }

  bool AddPage(wxWindow *page, const wxString &text, bool select = false,
               int imageId = wxWithImages::NO_IMAGE) {
    return InsertPage(GetPageCount(), page, text, select, imageId);
  }

  bool InsertPage(size_t n, wxWindow *page, const wxString &text,
                  bool select = false, int imageId = wxWithImages::NO_IMAGE) {
    if (!page || n > GetPageCount()) {
      return false;
    }

    m_pages.insert(m_pages.begin() + n, {page, text, imageId});
    if (m_selection != wxNOT_FOUND && static_cast<size_t>(m_selection) >= n) {
      ++m_selection;
    }

    if (select || m_selection == wxNOT_FOUND) {
      SelectPage(n, select);
    } else {
      page->Hide();
    }

    return true;
  }

  bool DeletePage(size_t n) {
    wxWindow *page = RemovePage(n);
    if (!page) {
      return false;
    }

    delete page;
    return true;
  }

  wxWindow *RemovePage(size_t n) {
    if (n >= GetPageCount()) {
      return nullptr;
    }

    wxWindow *page = m_pages[n].window;
    const bool wasSelected = m_selection == static_cast<int>(n);
    if (wasSelected) {
      page->Hide();
      m_selection = wxNOT_FOUND;
    }

    m_pages.erase(m_pages.begin() + n);
    if (wasSelected && !m_pages.empty()) {
      SelectPage(n < m_pages.size() ? n : m_pages.size() - 1, true);
    } else if (m_selection != wxNOT_FOUND &&
               static_cast<size_t>(m_selection) > n) {
      --m_selection;
    }

    return page;
  }

  bool DeleteAllPages() {
    while (!m_pages.empty()) {
      if (!DeletePage(m_pages.size() - 1)) {
        return false;
      }
    }
    return true;
  }

  // Sends wxEVT_BOOKCTRL_PAGE_CHANGING and wxEVT_BOOKCTRL_PAGE_CHANGED.
  // A handler can veto the changing event to keep the current selection.
  int SetSelection(size_t n) { return SelectPage(n, true); }

  // Changes the selection without generating wxBookCtrl events.
  int ChangeSelection(size_t n) { return SelectPage(n, false); }

  int FindPage(const wxWindow *page) const {
    for (size_t i = 0; i < GetPageCount(); ++i) {
      if (m_pages[i].window == page) {
        return static_cast<int>(i);
      }
    }
    return wxNOT_FOUND;
  }

  bool SetPageText(size_t n, const wxString &text) {
    if (n >= GetPageCount()) {
      return false;
    }
    m_pages[n].text = text;
    return true;
  }

  wxString GetPageText(size_t n) const {
    return n < GetPageCount() ? m_pages[n].text : wxString();
  }

  bool SetPageImage(size_t n, int imageId) {
    if (n >= GetPageCount()) {
      return false;
    }
    m_pages[n].imageId = imageId;
    return true;
  }

  int GetPageImage(size_t n) const {
    return n < GetPageCount() ? m_pages[n].imageId : wxWithImages::NO_IMAGE;
  }

  bool ShowNewPage(wxWindow *page) { return AddPage(page, wxString(), true); }

  void SetFocus() override {
    if (wxWindow *page = GetCurrentPage()) {
      page->SetFocus();
    }
  }

private:
  struct Page {
    wxWindow *window;
    wxString text;
    int imageId;
  };

  int SelectPage(size_t n, bool sendEvent) {
    if (n >= GetPageCount()) {
      return wxNOT_FOUND;
    }

    const int previous = m_selection;
    if (previous == static_cast<int>(n)) {
      return previous;
    }

    wxBookCtrlEvent event(wxEVT_BOOKCTRL_PAGE_CHANGING, GetId(),
                          static_cast<int>(n), previous);
    bool allowed = true;
    if (sendEvent) {
      event.SetEventObject(this);
      allowed = !GetEventHandler()->ProcessEvent(event) || event.IsAllowed();
    }

    if (!allowed) {
      return previous;
    }

    if (wxWindow *page = GetCurrentPage()) {
      page->Hide();
    }

    wxWindow *page = GetPage(n);
    page->SetSize(GetClientRect());
    page->Show();
    page->SetFocus();
    m_selection = static_cast<int>(n);

    if (sendEvent) {
      event.SetEventType(wxEVT_BOOKCTRL_PAGE_CHANGED);
      GetEventHandler()->ProcessEvent(event);
    }

    return previous;
  }

  void Init() { m_selection = wxNOT_FOUND; }

  void OnSize(wxSizeEvent &event) {
    if (wxWindow *page = GetCurrentPage()) {
      page->SetSize(GetClientRect());
    }
    event.Skip();
  }

  wxVector<Page> m_pages;
  int m_selection{wxNOT_FOUND};

  wxDECLARE_NO_COPY_CLASS(WindowBook);
};

#endif // KENNEL_WINDOWBOOK_HPP
