#include "app/FileEvent.hpp"

wxDEFINE_EVENT(wxEVT_FILE_SAVE_STARTED, FileEvent);
wxDEFINE_EVENT(wxEVT_FILE_SAVE_DONE, FileEvent);
wxDEFINE_EVENT(wxEVT_REMOTE_FILE_READ, wxThreadEvent);
