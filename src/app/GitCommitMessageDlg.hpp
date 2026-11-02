#pragma once

#include "UI.hpp"

class GitCommitMessageDlg : public GitCommitMessageDlgBase {
public:
  GitCommitMessageDlg(wxWindow *parent);
  ~GitCommitMessageDlg() override;
};
