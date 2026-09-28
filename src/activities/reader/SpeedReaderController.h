#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "SpeedReaderSettings.h"

class GfxRenderer;

// Supplies a book's words in reading order. Each reader (TXT, EPUB) implements one.
// Positions are opaque to the controller but must increase in reading order, so the
// controller can tell whether one position comes before another.
class SpeedReaderWordSource {
 public:
  virtual ~SpeedReaderWordSource() = default;

  // Position of the word the next nextWord() call returns.
  virtual uint64_t tell() const = 0;
  virtual bool seek(uint64_t position) = 0;
  // Copies the next word into buf (NUL-terminated, cut with SpeedReaderController::copyWord) and
  // advances. Sets paragraphEnd when the word is the last of its paragraph. False at book end.
  virtual bool nextWord(char* buf, size_t bufSize, bool& paragraphEnd) = 0;
  // A position before `position` from which reading forward reaches it, such as the start of
  // the previous page. Used to step back past the controller's history. False at book start.
  virtual bool anchorBefore(uint64_t position, uint64_t& anchor) = 0;
};

// Shared speed reader engine: groups words, times them, keeps a history for stepping back,
// and draws the current group centered on screen. The owning reader feeds it input, calls
// update() from its loop and draw() from its render, and handles stats and the status bar.
//
// Threading: draw() runs on the render task. Like the auto page turn, the reader calls
// update(), stepBack() and togglePause() only while no render is in progress
// (RenderLock::peek() is false), so the group text is never replaced mid-draw.
// Allocate it only while speed reading: its buffers take about 4 KB.
class SpeedReaderController {
 public:
  // Longer words are cut at a UTF-8 boundary; nothing that long is readable at speed anyway.
  static constexpr size_t MAX_WORD_BYTES = 64;
  static constexpr size_t GROUP_TEXT_BYTES = SpeedReaderSettings::MAX_WORDS_PER_GROUP * MAX_WORD_BYTES + 1;
  // A drawn line may hold the whole group with each single space widened to " · " (3 more bytes).
  static constexpr size_t LINE_TEXT_BYTES = GROUP_TEXT_BYTES + (SpeedReaderSettings::MAX_WORDS_PER_GROUP - 1) * 3;
  // Group starts remembered for stepping back (2 KB). Older groups are rebuilt from an anchor.
  static constexpr size_t HISTORY_SIZE = 256;

  void configure(const SpeedReaderSettings& settings);
  // Shows the first group at `position`, paused. Returns false if the source has no words there.
  bool start(SpeedReaderWordSource& source, uint64_t position);
  void stop();

  bool isActive() const { return source != nullptr; }
  bool isRunning() const { return running; }
  bool isAtEnd() const { return atEnd; }
  // Start of the group on screen; the reader maps it back to a page when speed reading ends.
  uint64_t currentGroupPosition() const { return groupStart; }
  const char* currentGroupText() const { return groupText.data(); }

  // Copies up to srcLen bytes of src into dst (always NUL-terminated), never splitting a UTF-8
  // character. Returns the number of bytes copied.
  static size_t copyWord(char* dst, size_t dstSize, const char* src, size_t srcLen);

  // Next Page: start or pause. Resuming at the end of the book does nothing.
  void togglePause(unsigned long nowMs);
  void pause();
  // Previous Page press: pauses and steps back one group. Returns true when the group changed.
  bool stepBack();
  // After the source ran out of words (isAtEnd) because more text was not ready yet, continue
  // from where it stopped, keeping the step-back history. Returns true when a group was loaded.
  bool continueAfterEnd(bool resume);

  // Call every loop. previousHeld/heldMs describe the Previous Page button so holding it keeps
  // stepping back, faster the longer it is held. Returns true when the screen needs redrawing.
  bool update(unsigned long nowMs, bool previousHeld, unsigned long heldMs);
  // Call after the current group has reached the panel; the display time counts from here.
  void onGroupDisplayed(unsigned long nowMs);

  // Draws the current group centered in the given area, wrapped onto several lines if needed,
  // plus a small "Paused" or "End of book" label under it. For SD-card fonts the caller wraps it
  // in a FontCacheManager prewarm scope (scan pass, then real pass), as page rendering does.
  void draw(GfxRenderer& renderer, int fontId, int left, int top, int width, int height, bool black) const;

 private:
  bool loadGroup();
  void pushHistory(uint64_t position);
  bool popHistory(uint64_t& position);
  bool rebuildHistoryBefore(uint64_t target);
  uint32_t currentIntervalMs() const;

  SpeedReaderWordSource* source = nullptr;
  SpeedReaderSettings settings;
  bool running = false;
  bool atEnd = false;
  bool groupEndsParagraph = false;
  uint64_t groupStart = 0;
  std::array<char, GROUP_TEXT_BYTES> groupText{};
  // Scratch buffers kept off the task stacks: loadGroup() runs on the loop task, draw() on the
  // render task, so each has its own.
  std::array<char, GROUP_TEXT_BYTES> pendingText{};
  mutable std::array<char, LINE_TEXT_BYTES> lineText{};

  std::array<uint64_t, HISTORY_SIZE> history{};
  size_t historyHead = 0;  // Next slot to write.
  size_t historyCount = 0;

  // Written by the render task after drawing, read by the loop; 0 while waiting for a redraw.
  std::atomic<unsigned long> groupShownAtMs{0};
  unsigned long lastHoldStepMs = 0;
};
