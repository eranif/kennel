#include "core/Globals.hpp"

static IManager *s_manager{nullptr};

void SetKennelManager(IManager *manager) { s_manager = manager; }

IManager *GetKennelManager() { return s_manager; }
