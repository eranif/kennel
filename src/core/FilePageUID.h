#pragma once

#include <cstdint>

// Tells a file page (an editor, see IEditor::GetEditorUID()) from every other
// one, for as long as it lives. Never zero.
using FilePageUID = uint64_t;
