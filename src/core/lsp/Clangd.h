#pragma once

#include "core/ToolDescription.h"

// clangd, the language server of C and C++.
//
// Only the program is installed (see RemoteInstaller), not the rest of the
// release: the headers of the compiler (lib/clang/<version>/include) come from
// the compiler of the machine, which clangd asks.
namespace clangd {

// The release Kennel was built for.
const ToolDescription &Tool();

} // namespace clangd
