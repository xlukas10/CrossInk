#include "EpubSpeedReaderSource.h"

#include <Logging.h>

#include <cstring>

namespace {
// The layout prefixes indented paragraph openers with an em space (U+2003); it is not a word.
constexpr char EM_SPACE[] = "\xe2\x80\x83";
constexpr size_t EM_SPACE_BYTES = sizeof(EM_SPACE) - 1;

void skipEmSpaces(const char*& text, size_t& length) {
  while (length >= EM_SPACE_BYTES && std::memcmp(text, EM_SPACE, EM_SPACE_BYTES) == 0) {
    text += EM_SPACE_BYTES;
    length -= EM_SPACE_BYTES;
  }
}
}  // namespace

uint64_t EpubSpeedReaderSource::makePosition(const uint16_t spine, const uint16_t page, const uint16_t element,
                                             const uint16_t word) {
  return (static_cast<uint64_t>(spine) << 48) | (static_cast<uint64_t>(page) << 32) |
         (static_cast<uint64_t>(element) << 16) | word;
}

EpubSpeedReaderSource::EpubSpeedReaderSource(const std::unique_ptr<Section>& section, const int& spineIndex)
    : section(section), spineIndex(spineIndex) {}

bool EpubSpeedReaderSource::seek(const uint64_t position) {
  spine = positionSpine(position);
  page = positionPage(position);
  element = static_cast<uint16_t>(position >> 16);
  word = static_cast<uint16_t>(position);
  lastStall = Stall::None;
  return true;
}

void EpubSpeedReaderSource::invalidate() {
  cachedPage.reset();
  cachedSection = nullptr;
  cachedPageIndex = -1;
}

bool EpubSpeedReaderSource::ensurePage(const uint16_t index) {
  Section* current = section.get();
  if (!current || static_cast<int>(spine) != spineIndex) {
    // The reader is between sections (chapter change or relayout); it restarts us afterwards.
    lastStall = Stall::WaitingForPages;
    return false;
  }
  if (cachedPage && cachedSection == current && cachedPageIndex == index) return true;

  if (index >= current->pageCount) {
    // A building or partial section's pageCount is only a watermark; more pages will follow.
    const bool complete = !current->isBuilding() && !current->isPartial();
    lastStall = complete ? Stall::ChapterEnd : Stall::WaitingForPages;
    return false;
  }
  auto loaded = current->loadPage(index);
  if (!loaded) {
    LOG_ERR("SPR", "Could not load EPUB page %u for the speed reader", index);
    lastStall = Stall::WaitingForPages;
    return false;
  }
  cachedPage = std::move(loaded);
  cachedSection = current;
  cachedPageIndex = index;
  return true;
}

const TextBlock* EpubSpeedReaderSource::currentLine() {
  while (true) {
    if (!ensurePage(page)) return nullptr;
    const auto& elements = cachedPage->elements;
    if (element >= elements.size()) {
      page++;
      element = 0;
      word = 0;
      continue;
    }
    // Only running text: images, table fragments and rules are skipped.
    const PageElement* candidate = elements[element].get();
    if (candidate->getTag() == TAG_PageLine) {
      const TextBlock* block = static_cast<const PageLine*>(candidate)->getBlock().get();
      if (block && word < block->wordCount()) return block;
    }
    element++;
    word = 0;
  }
}

bool EpubSpeedReaderSource::nextWord(char* buf, const size_t bufSize, bool& paragraphEnd) {
  paragraphEnd = false;
  lastStall = Stall::None;
  if (bufSize == 0) return false;

  while (true) {
    const TextBlock* line = currentLine();
    if (!line) return false;

    size_t used = 0;
    buf[0] = '\0';
    const auto appendWord = [&](const TextBlock& block, const uint16_t index) {
      const char* text = block.wordText(index);
      size_t length = block.wordTextLen(index);
      skipEmSpaces(text, length);
      used += SpeedReaderController::copyWord(buf + used, bufSize - used, text, length);
    };
    // Text split into several stored words without a space between (a style change inside a
    // word, trailing punctuation) is one word on screen.
    const auto appendAttachedWords = [&](const TextBlock& block) {
      while (word < block.wordCount() && !block.wordHasSpaceBefore(word)) {
        appendWord(block, word);
        word++;
      }
    };

    appendWord(*line, word);
    word++;
    appendAttachedWords(*line);

    // A word the layout hyphenated at the line end continues on the next line, possibly on the
    // next page. Drop the inserted hyphen and read the rest of the word.
    if (word == line->wordCount() && line->wordEndsWithInsertedHyphen(word - 1) && used > 0 && buf[used - 1] == '-') {
      used--;
      buf[used] = '\0';
      element++;
      word = 0;
      const TextBlock* next = currentLine();
      if (!next) {
        // The rest of the word is not laid out yet: show it hyphenated rather than waiting.
        used += SpeedReaderController::copyWord(buf + used, bufSize - used, "-", 1);
        return true;
      }
      line = next;
      appendWord(*line, word);
      word++;
      appendAttachedWords(*line);
    }

    paragraphEnd = word >= line->wordCount() && line->endsParagraph();
    if (used > 0) return true;
    // Nothing visible (e.g. only an em space); a paragraph end on it is not worth a pause.
  }
}

bool EpubSpeedReaderSource::anchorBefore(const uint64_t target, uint64_t& anchor) {
  const uint16_t targetSpine = positionSpine(target);
  const uint16_t targetPage = positionPage(target);
  if (targetPage > 0) {
    anchor = makePosition(targetSpine, targetPage - 1, 0, 0);
    return true;
  }
  // Stepping back stops at the start of the current chapter; earlier chapters are not loaded.
  if ((target & 0xFFFFFFFFULL) != 0) {
    anchor = makePosition(targetSpine, 0, 0, 0);
    return true;
  }
  return false;
}
