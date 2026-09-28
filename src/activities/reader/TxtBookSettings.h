#pragma once

#include <string>

#include "SpeedReaderSettings.h"

// Per-book settings for TXT and Markdown books, stored as reader_settings.bin in the
// book's txt_<hash> cache folder. EPUB keeps its own, larger file of the same name.
struct TxtBookSettings {
  bool hasSpeedReaderSettings = false;
  SpeedReaderSettings speedReader;

  // Returns defaults when the file is missing, from an unknown version, or truncated.
  static TxtBookSettings load(const std::string& cachePath);
  bool save(const std::string& cachePath) const;
};
