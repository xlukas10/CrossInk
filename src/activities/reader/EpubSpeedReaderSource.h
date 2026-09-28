#pragma once

#include <Epub/Page.h>
#include <Epub/Section.h>

#include <cstddef>
#include <cstdint>
#include <memory>

#include "SpeedReaderController.h"

// Speed reader words for EPUB books. Reads the reader's current section (one spine item)
// page by page, skipping images, tables and rules, and rejoins words the layout split with
// an inserted hyphen. It never builds or switches sections itself: when it runs out of pages it
// reports why (stall()), and the reader builds more pages or moves to the next chapter.
//
// All calls touch the section, so the reader makes them under its RenderLock.
class EpubSpeedReaderSource final : public SpeedReaderWordSource {
 public:
  enum class Stall : uint8_t {
    None,
    // The next page is not laid out yet (section still building) or the section is being rebuilt.
    WaitingForPages,
    // The section is complete and every word has been read.
    ChapterEnd,
  };

  // Positions pack spine, page, page element and word index, 16 bits each, so they increase
  // in reading order.
  static uint64_t makePosition(uint16_t spine, uint16_t page, uint16_t element, uint16_t word);
  static uint16_t positionSpine(uint64_t position) { return static_cast<uint16_t>(position >> 48); }
  static uint16_t positionPage(uint64_t position) { return static_cast<uint16_t>(position >> 32); }

  // Both references are the reader's own members, so the source follows section rebuilds.
  EpubSpeedReaderSource(const std::unique_ptr<Section>& section, const int& spineIndex);

  uint64_t tell() const override { return makePosition(spine, page, element, word); }
  bool seek(uint64_t position) override;
  bool nextWord(char* buf, size_t bufSize, bool& paragraphEnd) override;
  bool anchorBefore(uint64_t target, uint64_t& anchor) override;

  Stall stall() const { return lastStall; }
  // Drops the cached page; call after the section was reset or rebuilt.
  void invalidate();

 private:
  // Loads page `index` into cachedPage. On failure sets lastStall and returns false.
  bool ensurePage(uint16_t index);
  // Advances element/page to the next text line at or after the current one. False on stall.
  const TextBlock* currentLine();

  const std::unique_ptr<Section>& section;
  const int& spineIndex;
  uint16_t spine = 0;
  uint16_t page = 0;
  uint16_t element = 0;
  uint16_t word = 0;
  Stall lastStall = Stall::None;

  std::unique_ptr<Page> cachedPage;
  const Section* cachedSection = nullptr;
  int cachedPageIndex = -1;
};
