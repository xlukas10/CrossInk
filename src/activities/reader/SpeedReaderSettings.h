#pragma once

#include <algorithm>
#include <cstdint>

// Per-book speed reader state. Like the auto page turn interval there is no global
// setting: a book without saved values starts from these defaults.
struct SpeedReaderSettings {
  static constexpr uint8_t MIN_WORDS_PER_GROUP = 1;
  static constexpr uint8_t MAX_WORDS_PER_GROUP = 10;
  static constexpr uint8_t DEFAULT_WORDS_PER_GROUP = 1;
  // How long one word group stays on screen, in tenths of a second (0.1 s to 10 s).
  static constexpr uint16_t MIN_INTERVAL_TENTHS = 1;
  static constexpr uint16_t MAX_INTERVAL_TENTHS = 100;
  static constexpr uint16_t DEFAULT_INTERVAL_TENTHS = 3;

  bool enabled = false;
  uint8_t wordsPerGroup = DEFAULT_WORDS_PER_GROUP;
  uint16_t intervalTenths = DEFAULT_INTERVAL_TENTHS;
  // A middle dot between the words of a group. Separate from the page Guide Dots setting, so it
  // can differ and toggling it never re-lays out an EPUB. A single-word group shows no dot.
  bool guideDots = false;

  // Clamps values read from disk so a damaged or hand-edited file cannot stall or flood the reader.
  void normalize() {
    wordsPerGroup = std::clamp(wordsPerGroup, MIN_WORDS_PER_GROUP, MAX_WORDS_PER_GROUP);
    intervalTenths = std::clamp(intervalTenths, MIN_INTERVAL_TENTHS, MAX_INTERVAL_TENTHS);
  }

  uint32_t intervalMs() const { return static_cast<uint32_t>(intervalTenths) * 100U; }
};
