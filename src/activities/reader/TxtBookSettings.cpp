#include "TxtBookSettings.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstdint>

namespace {
constexpr char TXT_SETTINGS_FILE_NAME[] = "/reader_settings.bin";
constexpr uint8_t TXT_SETTINGS_FILE_VERSION = 1;
constexpr uint8_t TXT_SETTINGS_FLAG_SPEED_READER = 1 << 0;
// version, flags, speed reader enabled, words per group, interval (u16 LE)
constexpr size_t TXT_SETTINGS_FILE_SIZE = 6;
}  // namespace

TxtBookSettings TxtBookSettings::load(const std::string& cachePath) {
  TxtBookSettings settings;
  HalFile file;
  if (!Storage.openFileForRead("TBS", cachePath + TXT_SETTINGS_FILE_NAME, file)) {
    return settings;
  }

  uint8_t data[TXT_SETTINGS_FILE_SIZE];
  const bool complete = file.read(data, sizeof(data)) == static_cast<int>(sizeof(data));
  file.close();
  if (!complete) {
    LOG_ERR("TBS", "TXT book settings file is truncated, using defaults");
    return settings;
  }
  if (data[0] != TXT_SETTINGS_FILE_VERSION) {
    LOG_DBG("TBS", "TXT book settings version %u unknown, using defaults", data[0]);
    return settings;
  }

  if (data[1] & TXT_SETTINGS_FLAG_SPEED_READER) {
    settings.hasSpeedReaderSettings = true;
    settings.speedReader.enabled = data[2] != 0;
    settings.speedReader.wordsPerGroup = data[3];
    settings.speedReader.intervalTenths = static_cast<uint16_t>(data[4] | (data[5] << 8));
    settings.speedReader.normalize();
  }
  return settings;
}

bool TxtBookSettings::save(const std::string& cachePath) const {
  HalFile file;
  if (!Storage.openFileForWrite("TBS", cachePath + TXT_SETTINGS_FILE_NAME, file)) {
    LOG_ERR("TBS", "Could not open TXT book settings for write");
    return false;
  }

  SpeedReaderSettings normalized = speedReader;
  normalized.normalize();
  const uint8_t data[TXT_SETTINGS_FILE_SIZE] = {
      TXT_SETTINGS_FILE_VERSION,
      static_cast<uint8_t>(hasSpeedReaderSettings ? TXT_SETTINGS_FLAG_SPEED_READER : 0),
      static_cast<uint8_t>(normalized.enabled ? 1 : 0),
      normalized.wordsPerGroup,
      static_cast<uint8_t>(normalized.intervalTenths & 0xFF),
      static_cast<uint8_t>(normalized.intervalTenths >> 8),
  };
  const bool written = file.write(data, sizeof(data)) == sizeof(data);
  file.close();
  if (!written) {
    LOG_ERR("TBS", "Short write saving TXT book settings");
  }
  return written;
}
