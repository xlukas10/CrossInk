#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "SpeedReaderController.h"

class Txt;

// Speed reader words for TXT and Markdown books. Positions are byte offsets into the file.
// A paragraph ends at a line break, matching how the TXT reader lays out lines.
class TxtSpeedReaderSource final : public SpeedReaderWordSource {
 public:
  // pageOffsets is the reader's page index; it is only read, to find anchors when stepping back.
  TxtSpeedReaderSource(const Txt& txt, const std::vector<size_t>& pageOffsets);

  uint64_t tell() const override { return position; }
  bool seek(uint64_t newPosition) override;
  bool nextWord(char* buf, size_t bufSize, bool& paragraphEnd) override;
  bool anchorBefore(uint64_t target, uint64_t& anchor) override;

 private:
  // Returns the byte at offset, or -1 past the end of the file or on a read error.
  int byteAt(size_t offset);

  const Txt& txt;
  const std::vector<size_t>& pageOffsets;
  size_t fileSize = 0;
  size_t position = 0;
  // One file read serves ~150 words; each read reopens the file, as the TXT reader does.
  std::array<uint8_t, 1024> window{};
  size_t windowStart = 0;
  size_t windowLength = 0;
};
