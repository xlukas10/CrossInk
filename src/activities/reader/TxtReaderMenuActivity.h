#pragma once

#include <I18n.h>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

class TxtReaderMenuActivity final : public Activity {
 public:
  enum class MenuAction {
    GO_TO_PERCENT,
    AUTO_PAGE_TURN,
    READER_OPTIONS,
    TOGGLE_DARK_MODE,
    SCREENSHOT,
    READING_STATS,
    TOGGLE_COMPLETED,
    DELETE_STATS,
    DELETE_CACHE,
    SEND_NEARBY_BOOK,
    DISABLE_TOUCHSCREEN,
  };

  TxtReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                        bool isBookCompleted);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return true; }
  bool allowGlobalHomeGesture() const override { return false; }

 private:
  // One interaction per visible row; sized so every item fits even when the whole list is on screen.
  using UiHost = UiAppHost<12, 2>;
  using UiApp = UiHost::App;
  static constexpr size_t kMaxMenuItems = 11;

  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  static std::vector<MenuItem> buildMenuItems(bool isBookCompleted, bool hasTouch);
  void finishCancelled();

  ButtonNavigator buttonNavigator;
  std::string title;
  std::vector<MenuItem> items;
  std::array<freeink::ui::ListItem, kMaxMenuItems> listItems{};
  int selectedIndex = 0;
  UiHost ui;
  int visibleRows = 1;
  int topIndex = 0;
  int listHeaderHeight = 0;

  static void listScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildListScreen(UiApp::ScreenType& screen);
  void refreshListItems();
  void selectCurrent();
};
