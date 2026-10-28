#include "core/FileLocation.h"

namespace {
// The number that `text` holds, if it is made of digits only (and not huge).
bool ParseNumber(const wxString &text, int *number) {
  constexpr size_t kMaxDigits = 9;
  if (text.empty() || text.length() > kMaxDigits) {
    return false;
  }
  int value = 0;
  for (const wxUniChar ch : text) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    value = value * 10 + static_cast<int>(ch.GetValue() - '0');
  }
  *number = value;
  return true;
}
} // namespace

FileLocation ParseFileLocation(const wxString &text) {
  // Up to two numbers from the end, the last one first: [column, line].
  int numbers[2] = {0, 0};
  int count = 0;
  wxString rest = text;
  while (count < 2) {
    const int colon = rest.Find(':', /*fromEnd=*/true);
    // A colon at 0 or 1 is no separator: nothing is left of the path, or just
    // a drive letter.
    if (colon < 2 || !ParseNumber(rest.Mid(colon + 1), &numbers[count])) {
      break;
    }
    rest = rest.Left(colon);
    ++count;
  }

  FileLocation location;
  location.path = rest;
  if (count == 1) {
    location.line = numbers[0];
  } else if (count == 2) {
    location.line = numbers[1];
    location.column = numbers[0];
  }
  return location;
}

bool ParseLineColumn(const wxString &text, int *line, int *column) {
  wxString lineText = text;
  wxString columnText;
  const int colon = text.Find(':');
  if (colon != wxNOT_FOUND) {
    lineText = text.Left(colon);
    columnText = text.Mid(colon + 1);
  }
  lineText.Trim().Trim(false);
  columnText.Trim().Trim(false);

  int parsedLine = 0;
  int parsedColumn = 0;
  if (!ParseNumber(lineText, &parsedLine) || parsedLine < 1) {
    return false;
  }
  if (!columnText.empty() && !ParseNumber(columnText, &parsedColumn)) {
    return false;
  }
  *line = parsedLine;
  *column = parsedColumn;
  return true;
}
