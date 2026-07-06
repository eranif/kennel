#ifndef EDITHOSTS_HPP
#define EDITHOSTS_HPP
#include "UI.hpp"
#include "core/SshConfigParser.h"

enum class EditHostsMode {
  kEdit,
  kInsert,
};

class EditHosts : public EditHostsBase {
public:
  EditHosts(wxWindow *parent, EditHostsMode mode);
  ~EditHosts() override;

  struct HostClientData {
    bool can_delete{true};
  };

  const SshHostEntry &GetSelection() const { return m_selectedEntry; }

protected:
  void OnHostSelected(wxDataViewEvent &event) override;
  void OnHostActivated(wxDataViewEvent &event) override;
  bool UpdateSelection(const wxDataViewItem &item);
  void InitHosts();
  void DeleteAll();
  void SaveUserHosts();
  EditHosts::HostClientData *GetHostData(const wxDataViewItem &item) const;
  void AddHost(const wxString &name, const wxString &address, bool can_delete);

  void OnDeleteUI(wxUpdateUIEvent &event) override;
  void OnDelete(wxCommandEvent &event) override;
  void OnNew(wxCommandEvent &event) override;
  void OnOK(wxCommandEvent &event);

private:
  SshHostEntry m_selectedEntry;
  EditHostsMode m_mode;
};
#endif // EDITHOSTS_HPP
