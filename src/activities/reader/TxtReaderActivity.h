#pragma once

#include <Txt.h>

#include <vector>

#include "BookReadingStats.h"
#include "CrossPointSettings.h"
#include "GlobalReadingStats.h"
#include "ReaderProgressSaveDebouncer.h"
#include "SpeedReaderController.h"
#include "TxtBookSettings.h"
#include "TxtReaderMenuActivity.h"
#include "TxtSpeedReaderSource.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#if CROSSINK_APP_CAP_TOUCH
#include "activities/reader/ReaderPinchGesture.h"
#endif

class TxtReaderActivity final : public Activity {
  OptionPopup quickActionsPopup;
  std::unique_ptr<Txt> txt;

  int currentPage = 0;
  int totalPages = 1;
  int pagesUntilFullRefresh = 0;
  // Session-only display toggle; cached page layout remains unchanged.
  bool statusBarVisible = true;
  bool sideButtonLongPressHandled = false;
  bool frontButtonLongPressHandled = false;
  bool longPowerButtonHandled = false;
  bool longPressBackHandled = false;
  bool longPressMenuHandled = false;
  bool skipRecentBookUpdateOnEntry = false;
  // Set from the menu; the render task takes the screenshot after the next page is drawn.
  bool pendingScreenshot = false;
  ReaderProgressSaveDebouncer progressSaveDebouncer;

  // Reading stats, using the same session model as the XTC reader.
  unsigned long pageShownAtMs = 0UL;
  uint32_t sessionReadingSeconds = 0;
  BookReadingStats stats;
  GlobalReadingStats globalStats;
  ReadingStatsDateTime sessionStartLocalDateTime;
  bool hasSessionStartLocalDateTime = false;

  // Auto page turn is session-only: it stops on exit and is not saved per book.
  bool autoPageTurnActive = false;
  uint16_t autoPageTurnSeconds = 0;
  unsigned long lastAutoPageTurnMs = 0UL;

  // Per-book settings (speed reader). The speed reader objects exist only while it is on, so a
  // normal reading session carries no extra RAM. The render task reads them, so they are
  // created and destroyed under RenderLock.
  TxtBookSettings bookSettings;
  std::unique_ptr<TxtSpeedReaderSource> speedReaderSource;
  std::unique_ptr<SpeedReaderController> speedReader;
  // The book was left in speed reader mode; start it once the page index is ready.
  bool pendingSpeedReaderStart = false;
  // Page-turn input that arrived while a group was being drawn, applied on the next idle loop.
  bool speedReaderPendingToggle = false;
  uint8_t speedReaderPendingSteps = 0;
  // Page shown by the last speed reader render; a new page takes the periodic full-refresh count.
  int speedReaderRenderedPage = -1;
#if CROSSINK_APP_CAP_TOUCH
  ReaderPinchGesture pinchFontGesture;
#endif

  // Streaming text reader - stores file offsets for each page
  std::vector<size_t> pageOffsets;  // File offset for start of each page
  std::vector<std::string> currentPageLines;
  int linesPerPage = 0;
  int viewportWidth = 0;
  bool initialized = false;

  // Cached settings for cache validation (different fonts/margins require re-indexing)
  int cachedFontId = 0;
  uint8_t cachedVerticalMargin = 0;
  uint8_t cachedHorizontalMargin = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  // Global Guide Dots setting when the page index was built; dots widen gaps and change line breaks.
  bool cachedGuideDots = false;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;

  void renderPage();
  void renderStatusBar() const;

  void initializeReader();
  bool loadPageAtOffset(size_t offset, std::vector<std::string>& outLines, size_t& nextOffset);
  void buildPageIndex();
  bool loadPageIndexCache();
  void savePageIndexCache() const;
  bool saveProgress(int page);
  bool queueProgressSave();
  bool flushQueuedProgress();
  void loadProgress();
  void toggleDarkMode();
  void toggleHomeButtonInReader();
  bool consumeLongPowerButtonRelease();
  bool consumeLongPowerButtonHold();
  static bool supportsQuickAction(CrossPointSettings::SHORT_PWRBTN action);
  bool executeReaderShortcutAction(CrossPointSettings::SHORT_PWRBTN action);
  bool executePowerButtonAction();
  bool executeLongPressBackAction();
  bool changeReaderFontSize(bool larger, FontSizeStepMode mode = FontSizeStepMode::Wrap);
  void cycleReaderFont();
  void rebuildTextLayout();
  void resetTextLayout();
  bool goToNextPage(bool recordPace);
  void goToPreviousPage();
  void stopAutoPageTurn();

  void pauseReadingStatsTimer();
  void resumeReadingStatsTimer();
  bool currentPageReadingSecondsForStats(uint32_t& seconds) const;
  bool forwardPageReadElapsed(uint32_t& seconds) const;
  void recordCurrentPageReadingTime();
  void recordForwardPageTurn(uint32_t seconds, bool recordPace);
  void commitReadingStats();
  void setBookCompleted(bool isCompleted);
  float getCurrentBookProgressPercent() const;

  void openReaderMenu();
  void onReaderMenuConfirm(TxtReaderMenuActivity::MenuAction action);
  void openGoToPercent();
  void openAutoPageTurnPicker();
  void openReaderOptions();
  void openReadingStats();
  void deleteBookStats();
  void deleteBookCache();

  void openSpeedReaderSettings();
  bool startSpeedReader();
  void stopSpeedReader();
  void updateSpeedReader(bool touchPrev, bool touchNext);
  void syncPageToSpeedReader();
  void renderSpeedReader();
  int pageForOffset(size_t offset) const;
#if CROSSINK_APP_CAP_TOUCH
  bool handlePinchFontResize();
  void resetPinchFontGesture();
#endif

 public:
  explicit TxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Txt> txt,
                             int initialRefreshCountdown, bool skipRecentBookUpdateOnEntry = false)
      : Activity("TxtReader", renderer, mappedInput),
        txt(std::move(txt)),
        pagesUntilFullRefresh(initialRefreshCountdown),
        skipRecentBookUpdateOnEntry(skipRecentBookUpdateOnEntry) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool handleTwoFingerSwipeAction(CrossPointSettings::TWO_FINGER_SWIPE_ACTION action) override;
  bool handleTwoFingerRotation(bool clockwise) override;
  bool prepareManualRefresh() override {
    pagesUntilFullRefresh = -1;
    return true;
  }
  bool isReaderActivity() const override { return true; }
  bool preventAutoSleep() override { return autoPageTurnActive || (speedReader && speedReader->isRunning()); }
  bool openReaderSettingsMenu() override {
    if (!txt) {
      return false;
    }
    openReaderMenu();
    return true;
  }
  bool usesFullScreenReaderVerticalSwipes() const override {
#if defined(FREEINK_DEVICE_STICKY) && FREEINK_DEVICE_STICKY
    return true;
#else
    return false;
#endif
  }
  bool canSnapshotForSleepOverlay() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return quickActionsPopup.isActive(); }
  bool blocksGlobalInput() const override { return quickActionsPopup.isActive(); }
  bool handleShortcutAction(uint8_t action) override;
  bool handleShortcutAction(CrossPointSettings::SHORT_PWRBTN action) override;
  std::string getCurrentBookPath() const override { return txt ? txt->getPath() : std::string{}; }
  std::string getCurrentBookTitle() const override { return txt ? txt->getTitle() : std::string{}; }
  bool getFrontlightPanelBookDetails(FrontlightPanelBookDetails& details) override;
  std::unique_ptr<Activity> createFrontlightReadingStatsActivity() override;
  void onFrontlightPanelOpened() override {
    if (speedReader) speedReader->pause();
    pauseReadingStatsTimer();
  }
  void onFrontlightPanelClosed() override;
  bool handleFrontlightPanelResult(const FrontlightPanelResult& result) override;

  // Renders the last saved page to the frame buffer without flushing to display.
  // Used by SleepActivity to prepare the background for the overlay sleep mode.
  // Returns false if the page cannot be loaded (missing cache / file error).
  static bool drawCurrentPageToBuffer(const std::string& filePath, GfxRenderer& renderer);
  ScreenshotInfo getScreenshotInfo() const override;
};
