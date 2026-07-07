#pragma once

// Human-readable application name (display + window title).
inline constexpr const char *kAppName = "Kennel";

// Semantic version string, kept in sync with the CMake project version.
inline constexpr const char *kAppVersion = "1.1.0";

// Returns the application version string.
const char *Version();
