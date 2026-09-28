#include "SpeedReaderController.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>

#include "fontIds.h"

namespace {
// Holding Previous Page starts repeating after this delay, then speeds up in two stages.
constexpr unsigned long HOLD_REPEAT_DELAY_MS = 500;
constexpr unsigned long HOLD_FAST_AFTER_MS = 2000;
constexpr unsigned long HOLD_FASTEST_AFTER_MS = 4000;
constexpr unsigned long HOLD_REPEAT_MS = 300;
constexpr unsigned long HOLD_FAST_REPEAT_MS = 150;
constexpr unsigned long HOLD_FASTEST_REPEAT_MS = 80;

struct WordSpan {
  uint16_t start;
  uint16_t length;
};

size_t appendSpan(char* dst, size_t dstSize, size_t used, const char* src, size_t length) {
  if (used + length + 1 > dstSize) return used;
  std::memcpy(dst + used, src, length);
  used += length;
  dst[used] = '\0';
  return used;
}
}  // namespace

size_t SpeedReaderController::copyWord(char* dst, const size_t dstSize, const char* src, const size_t srcLen) {
  if (dstSize == 0) return 0;
  size_t length = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  // Step back over UTF-8 continuation bytes so a cut never leaves half a character.
  if (length < srcLen) {
    while (length > 0 && (static_cast<uint8_t>(src[length]) & 0xC0) == 0x80) length--;
  }
  std::memcpy(dst, src, length);
  dst[length] = '\0';
  return length;
}

void SpeedReaderController::configure(const SpeedReaderSettings& newSettings) {
  settings = newSettings;
  settings.normalize();
}

bool SpeedReaderController::start(SpeedReaderWordSource& wordSource, const uint64_t position) {
  source = &wordSource;
  historyHead = 0;
  historyCount = 0;
  running = false;
  atEnd = false;
  groupShownAtMs.store(0);
  groupText[0] = '\0';
  if (!source->seek(position) || !loadGroup()) {
    source = nullptr;
    return false;
  }
  return true;
}

void SpeedReaderController::stop() {
  source = nullptr;
  running = false;
  atEnd = false;
  historyCount = 0;
  groupShownAtMs.store(0);
}

void SpeedReaderController::togglePause(const unsigned long nowMs) {
  if (!source || atEnd) return;
  running = !running;
  // Resuming gives the group on screen a full display time again.
  if (running) groupShownAtMs.store(nowMs);
}

void SpeedReaderController::pause() { running = false; }

bool SpeedReaderController::stepBack() {
  if (!source) return false;
  pause();
  uint64_t previous = 0;
  if (!popHistory(previous)) {
    if (!rebuildHistoryBefore(groupStart) || !popHistory(previous)) return false;
  }
  if (!source->seek(previous) || !loadGroup()) return false;
  atEnd = false;
  return true;
}

bool SpeedReaderController::continueAfterEnd(const bool resume) {
  if (!source || !atEnd) return false;
  const uint64_t previousStart = groupStart;
  if (!loadGroup()) return false;
  pushHistory(previousStart);
  atEnd = false;
  running = resume;
  // The display time starts once the new group has been drawn.
  groupShownAtMs.store(0);
  return true;
}

bool SpeedReaderController::update(const unsigned long nowMs, const bool previousHeld, const unsigned long heldMs) {
  if (!source) return false;

  if (!previousHeld) {
    lastHoldStepMs = 0;
  } else if (heldMs >= HOLD_REPEAT_DELAY_MS) {
    const unsigned long repeatMs = heldMs >= HOLD_FASTEST_AFTER_MS ? HOLD_FASTEST_REPEAT_MS
                                   : heldMs >= HOLD_FAST_AFTER_MS  ? HOLD_FAST_REPEAT_MS
                                                                   : HOLD_REPEAT_MS;
    if (lastHoldStepMs == 0 || nowMs - lastHoldStepMs >= repeatMs) {
      lastHoldStepMs = nowMs;
      // Steps can outpace the panel; the render simply shows the newest group.
      return stepBack();
    }
    return false;
  }

  if (!running || atEnd) return false;
  const unsigned long shownAt = groupShownAtMs.load();
  // Zero means the group has not reached the panel yet, so its display time has not started.
  if (shownAt == 0 || nowMs - shownAt < currentIntervalMs()) return false;

  const uint64_t previousStart = groupStart;
  if (!loadGroup()) {
    atEnd = true;
    running = false;
    return true;  // Redraw with the end-of-book label.
  }
  pushHistory(previousStart);
  groupShownAtMs.store(0);
  return true;
}

void SpeedReaderController::onGroupDisplayed(const unsigned long nowMs) {
  // millis() can be 0 right after boot; nudge it so 0 keeps meaning "not shown yet".
  groupShownAtMs.store(nowMs == 0 ? 1 : nowMs);
}

bool SpeedReaderController::loadGroup() {
  const uint64_t start = source->tell();
  // Build into pendingText so reaching the end of the book leaves the last group on screen.
  pendingText[0] = '\0';
  char word[MAX_WORD_BYTES];
  size_t used = 0;
  uint8_t count = 0;
  bool paragraphEnd = false;
  while (count < settings.wordsPerGroup && source->nextWord(word, sizeof(word), paragraphEnd)) {
    if (word[0] == '\0') continue;
    if (used > 0) used = appendSpan(pendingText.data(), pendingText.size(), used, " ", 1);
    used = appendSpan(pendingText.data(), pendingText.size(), used, word, std::strlen(word));
    count++;
    if (paragraphEnd) break;
  }
  if (count == 0) return false;

  groupStart = start;
  groupEndsParagraph = paragraphEnd;
  groupText = pendingText;
  return true;
}

void SpeedReaderController::pushHistory(const uint64_t position) {
  history[historyHead] = position;
  historyHead = (historyHead + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) historyCount++;
}

bool SpeedReaderController::popHistory(uint64_t& position) {
  if (historyCount == 0) return false;
  historyHead = (historyHead + HISTORY_SIZE - 1) % HISTORY_SIZE;
  historyCount--;
  position = history[historyHead];
  return true;
}

bool SpeedReaderController::rebuildHistoryBefore(const uint64_t target) {
  uint64_t anchor = 0;
  if (!source->anchorBefore(target, anchor) || !source->seek(anchor)) return false;

  // Replay groups from the anchor so stepping back walks the same boundaries forward reading makes.
  historyHead = 0;
  historyCount = 0;
  char word[MAX_WORD_BYTES];
  while (true) {
    const uint64_t start = source->tell();
    if (start >= target) break;
    uint8_t count = 0;
    bool paragraphEnd = false;
    while (count < settings.wordsPerGroup && source->nextWord(word, sizeof(word), paragraphEnd)) {
      count++;
      if (paragraphEnd) break;
    }
    if (count == 0) break;
    pushHistory(start);
  }
  return historyCount > 0;
}

uint32_t SpeedReaderController::currentIntervalMs() const {
  const uint32_t base = settings.intervalMs();
  // Linger on the last group of a paragraph so the break registers.
  return groupEndsParagraph ? base + base / 2 : base;
}

void SpeedReaderController::draw(GfxRenderer& renderer, const int fontId, const int left, const int top,
                                 const int width, const int height, const bool black) const {
  const char* text = groupText.data();
  if (renderer.isSdCardFont(fontId)) {
    renderer.ensureSdCardFontReady(fontId, text, /*styleMask=*/0x01);
  }

  std::array<WordSpan, SpeedReaderSettings::MAX_WORDS_PER_GROUP> words{};
  size_t wordCount = 0;
  for (size_t i = 0; text[i] != '\0' && wordCount < words.size();) {
    while (text[i] == ' ') i++;
    const size_t start = i;
    while (text[i] != '\0' && text[i] != ' ') i++;
    if (i > start) words[wordCount++] = {static_cast<uint16_t>(start), static_cast<uint16_t>(i - start)};
  }
  if (wordCount == 0) return;

  // Greedy wrap: each line holds as many words as fit, and every line is centered.
  std::array<uint8_t, SpeedReaderSettings::MAX_WORDS_PER_GROUP + 1> lineStarts{};
  size_t lineCount = 0;
  char* line = lineText.data();
  const size_t lineSize = lineText.size();
  size_t lineWords = 0;
  size_t used = 0;
  for (size_t w = 0; w < wordCount; ++w) {
    size_t candidate = used;
    if (lineWords > 0) candidate = appendSpan(line, lineSize, candidate, " ", 1);
    candidate = appendSpan(line, lineSize, candidate, text + words[w].start, words[w].length);
    if (lineWords > 0 && renderer.getTextWidth(fontId, line) > width) {
      lineStarts[lineCount++] = static_cast<uint8_t>(w - lineWords);
      lineWords = 0;
      used = appendSpan(line, lineSize, 0, text + words[w].start, words[w].length);
    } else {
      used = candidate;
    }
    lineWords++;
  }
  lineStarts[lineCount++] = static_cast<uint8_t>(wordCount - lineWords);
  lineStarts[lineCount] = static_cast<uint8_t>(wordCount);

  const int lineHeight = renderer.getLineHeight(fontId);
  const int blockHeight = static_cast<int>(lineCount) * lineHeight;
  int y = top + (height - blockHeight) / 2;
  for (size_t l = 0; l < lineCount; ++l) {
    used = 0;
    line[0] = '\0';
    for (size_t w = lineStarts[l]; w < lineStarts[l + 1]; ++w) {
      if (w > lineStarts[l]) used = appendSpan(line, lineSize, used, " ", 1);
      used = appendSpan(line, lineSize, used, text + words[w].start, words[w].length);
    }
    const int x = left + (width - renderer.getTextWidth(fontId, line)) / 2;
    renderer.drawText(fontId, x, y, line, black);
    y += lineHeight;
  }

  const char* label = atEnd ? tr(STR_END_OF_BOOK) : (!running ? tr(STR_SPEED_READER_PAUSED) : nullptr);
  if (label) {
    const int labelX = left + (width - renderer.getTextWidth(UI_10_FONT_ID, label)) / 2;
    renderer.drawText(UI_10_FONT_ID, labelX, y + lineHeight / 2, label, black);
  }
}
