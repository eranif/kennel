#pragma once

// The language of a file that a language server can have (its `languageId` in
// `didOpen` is written from it). GUI-free.
enum class LspLanguage {
  kNone, // A language that has no language server
  kC,
  kCpp,
  kObjectiveC,
  kObjectiveCpp,
  kGo,
  kRust,
  kPython,
  kPhp,
};
