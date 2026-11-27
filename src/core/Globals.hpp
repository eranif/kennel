#pragma once

class IManager;

// The application gives the core its IManager once (null again when the
// application ends), and the core reaches it from anywhere.
void SetKennelManager(IManager *manager);
IManager *GetKennelManager();
