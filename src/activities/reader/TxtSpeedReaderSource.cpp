#include "TxtSpeedReaderSource.h"

#include <Logging.h>
#include <Txt.h>

#include <algorithm>

namespace {
constexpr uint8_t UTF8_BOM[] = {0xEF, 0xBB, 0xBF};

bool isWordSeparator(const int byte) { return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n'; }
bool isInlineSpace(const int byte) { return byte == ' ' || byte == '\t' || byte == '\r'; }
}  // namespace

TxtSpeedReaderSource::TxtSpeedReaderSource(const Txt& txt, const std::vector<size_t>& pageOffsets)
    : txt(txt), pageOffsets(pageOffsets), fileSize(txt.getFileSize()) {}

bool TxtSpeedReaderSource::seek(const uint64_t newPosition) {
  if (newPosition > fileSize) return false;
  position = static_cast<size_t>(newPosition);
  return true;
}

int TxtSpeedReaderSource::byteAt(const size_t offset) {
  if (offset >= fileSize) return -1;
  if (offset < windowStart || offset >= windowStart + windowLength) {
    const size_t length = std::min(window.size(), fileSize - offset);
    if (!txt.readContent(window.data(), offset, length)) {
      LOG_ERR("SPR", "TXT read failed at offset %u", static_cast<unsigned>(offset));
      windowLength = 0;
      return -1;
    }
    windowStart = offset;
    windowLength = length;
  }
  return window[offset - windowStart];
}

bool TxtSpeedReaderSource::nextWord(char* buf, const size_t bufSize, bool& paragraphEnd) {
  paragraphEnd = false;
  if (bufSize == 0) return false;

  size_t pos = position;
  // A UTF-8 byte order mark is invisible in editors; do not show it as a word.
  if (pos == 0 && byteAt(0) == UTF8_BOM[0] && byteAt(1) == UTF8_BOM[1] && byteAt(2) == UTF8_BOM[2]) pos = 3;
  while (isWordSeparator(byteAt(pos))) pos++;
  if (byteAt(pos) < 0) {
    position = pos;
    return false;
  }

  // Copy the word byte by byte: it may cross a window refill, and it is cut if too long.
  size_t length = 0;
  bool truncated = false;
  for (int byte = byteAt(pos); byte >= 0 && !isWordSeparator(byte); byte = byteAt(++pos)) {
    if (length + 1 < bufSize) {
      buf[length++] = static_cast<char>(byte);
    } else {
      truncated = true;
    }
  }
  if (truncated) {
    // Drop a character that was cut in half: back over continuation bytes, then its lead byte.
    while (length > 0 && (static_cast<uint8_t>(buf[length - 1]) & 0xC0) == 0x80) length--;
    if (length > 0 && static_cast<uint8_t>(buf[length - 1]) >= 0xC0) length--;
  }
  buf[length] = '\0';

  size_t after = pos;
  while (isInlineSpace(byteAt(after))) after++;
  const int next = byteAt(after);
  paragraphEnd = next < 0 || next == '\n';

  position = pos;
  return true;
}

bool TxtSpeedReaderSource::anchorBefore(const uint64_t target, uint64_t& anchor) {
  if (target == 0 || pageOffsets.empty()) return false;
  // Page holding the byte just before target, then one page earlier, so the replay always
  // produces at least one full page of groups to step back through.
  const auto it = std::upper_bound(pageOffsets.begin(), pageOffsets.end(), static_cast<size_t>(target - 1));
  const size_t page = it == pageOffsets.begin() ? 0 : static_cast<size_t>(it - pageOffsets.begin()) - 1;
  anchor = pageOffsets[page > 0 ? page - 1 : 0];
  return anchor < target;
}
