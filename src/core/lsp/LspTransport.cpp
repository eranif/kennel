#include "core/lsp/LspTransport.h"

wxDEFINE_EVENT(wxEVT_LSP_TRANSPORT_OPENED, LspTransportEvent);
wxDEFINE_EVENT(wxEVT_LSP_TRANSPORT_STDOUT, LspTransportEvent);
wxDEFINE_EVENT(wxEVT_LSP_TRANSPORT_STDERR, LspTransportEvent);
wxDEFINE_EVENT(wxEVT_LSP_TRANSPORT_CLOSED, LspTransportEvent);
