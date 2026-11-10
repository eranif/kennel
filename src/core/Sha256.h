#pragma once

#include <string>

// SHA-256 of `data` as 64 lower-case hex digits. For checking a downloaded file
// against a checksum that is pinned in the source (see ToolDescription.h).
std::string Sha256Hex(const std::string &data);
