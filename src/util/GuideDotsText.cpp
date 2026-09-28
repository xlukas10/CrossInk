#include "GuideDotsText.h"

namespace {
bool isGap(const char c) { return c == ' ' || c == '\t'; }
}  // namespace

std::string GuideDotsText::withDots(const std::string& line) {
  std::string out;
  out.reserve(line.size() + line.size() / 4);
  size_t i = 0;
  // Leading whitespace is indentation, not a gap between words.
  while (i < line.size() && isGap(line[i])) out.push_back(line[i++]);
  while (i < line.size()) {
    if (!isGap(line[i])) {
      out.push_back(line[i++]);
      continue;
    }
    const size_t gapStart = i;
    while (i < line.size() && isGap(line[i])) i++;
    if (i == line.size()) {
      // Trailing whitespace: keep it as it was.
      out.append(line, gapStart, i - gapStart);
    } else {
      out.append(SEPARATOR);
    }
  }
  return out;
}
