#pragma once

#include <array>
#include <cstddef>

#include "SpeedReaderSettings.h"
#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

// Book menu screen for the speed reader: On/Off, words per group, time per group and Guide Dots.
// Back returns the edited values as a SpeedReaderSettingsResult; the reader saves them for the
// book and starts or stops speed reading.
class SpeedReaderSettingsActivity final : public Activity {
 public:
  SpeedReaderSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                              const SpeedReaderSettings& initialSettings);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return true; }
  bool allowGlobalHomeGesture() const override { return false; }

 private:
  enum class Row { Enabled, WordsPerGroup, IntervalTenths, GuideDots, Count };
  static constexpr size_t kRowCount = static_cast<size_t>(Row::Count);

  using UiHost = UiAppHost<kRowCount, 2>;
  using UiApp = UiHost::App;

  static void listScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  static void formatIntervalTenths(int value, char* buf, size_t len);
  void buildListScreen(UiApp::ScreenType& screen);
  void refreshListItems();
  void selectCurrent();
  void finishWithSettings();
  void openWordsPicker();
  void openIntervalPicker();

  SpeedReaderSettings settings;
  ButtonNavigator buttonNavigator;
  UiHost ui;
  std::array<freeink::ui::ListItem, kRowCount> listItems{};
  // ListItem keeps pointers, so the formatted values live here.
  std::array<char, 8> wordsValue{};
  std::array<char, 16> intervalValue{};
  int selectedIndex = 0;
  int topIndex = 0;
  int visibleRows = 1;
  int listHeaderHeight = 0;
};
