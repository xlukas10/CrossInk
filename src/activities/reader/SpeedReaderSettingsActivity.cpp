#include "SpeedReaderSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr fui::ActionId kActionRow = 1;
constexpr int kNoteMaxLines = 3;
}  // namespace

SpeedReaderSettingsActivity::SpeedReaderSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                         const SpeedReaderSettings& initialSettings)
    : Activity("SpeedReaderSettings", renderer, mappedInput), settings(initialSettings), ui(renderer) {
  settings.normalize();
}

void SpeedReaderSettingsActivity::formatIntervalTenths(const int value, char* buf, const size_t len) {
  const unsigned tenths = static_cast<unsigned>(std::max(0, value));
  snprintf(buf, len, tr(STR_SPEED_READER_INTERVAL_FORMAT), tenths / 10, tenths % 10);
}

void SpeedReaderSettingsActivity::onEnter() {
  Activity::onEnter();
  mappedInput.setReaderTouchscreenOverride(true);
  selectedIndex = 0;
  topIndex = 0;
  visibleRows = 1;
  refreshListItems();
  ui.reset();
  ui.app.on(kActionRow, &SpeedReaderSettingsActivity::onRowEvent, this);
  ui.app.setScreen(&SpeedReaderSettingsActivity::listScreen, this);
  requestUpdate();
}

void SpeedReaderSettingsActivity::onExit() {
  mappedInput.setReaderTouchscreenOverride(false);
  Activity::onExit();
}

void SpeedReaderSettingsActivity::finishWithSettings() {
  setResult(SpeedReaderSettingsResult{settings});
  finish();
}

void SpeedReaderSettingsActivity::openWordsPicker() {
  auto picker = makeUniqueNoThrow<IntervalSelectionActivity>(
      renderer, mappedInput, "SpeedReaderWordsPerGroup", StrId::STR_SPEED_READER_WORDS_PER_GROUP,
      settings.wordsPerGroup, SpeedReaderSettings::MIN_WORDS_PER_GROUP, SpeedReaderSettings::MAX_WORDS_PER_GROUP, 1, 3,
      StrId::STR_NONE_OPT, /*readerActivity=*/true, /*allowPowerAsConfirm=*/true,
      /*ignoreInitialConfirmRelease=*/false, /*showPercentValue=*/false, StrId::STR_NONE_OPT,
      /*overrideDisabledReaderTouchscreen=*/true, /*showTouchHeaderBackButton=*/false, /*valueFormatter=*/nullptr,
      /*tapStep=*/1);
  if (!picker) {
    LOG_ERR("SPR", "OOM: words per group picker");
    return;
  }
  startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
    if (const auto* interval = std::get_if<IntervalResult>(&result.data); !result.isCancelled && interval) {
      settings.wordsPerGroup = static_cast<uint8_t>(interval->value);
      settings.normalize();
    }
    requestUpdate();
  });
}

void SpeedReaderSettingsActivity::openIntervalPicker() {
  auto picker = makeUniqueNoThrow<IntervalSelectionActivity>(
      renderer, mappedInput, "SpeedReaderTimePerGroup", StrId::STR_SPEED_READER_INTERVAL, settings.intervalTenths,
      SpeedReaderSettings::MIN_INTERVAL_TENTHS, SpeedReaderSettings::MAX_INTERVAL_TENTHS, 1, 10, StrId::STR_NONE_OPT,
      /*readerActivity=*/true, /*allowPowerAsConfirm=*/true, /*ignoreInitialConfirmRelease=*/false,
      /*showPercentValue=*/false, StrId::STR_NONE_OPT, /*overrideDisabledReaderTouchscreen=*/true,
      /*showTouchHeaderBackButton=*/false, &SpeedReaderSettingsActivity::formatIntervalTenths, /*tapStep=*/1);
  if (!picker) {
    LOG_ERR("SPR", "OOM: time per group picker");
    return;
  }
  startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
    if (const auto* interval = std::get_if<IntervalResult>(&result.data); !result.isCancelled && interval) {
      settings.intervalTenths = static_cast<uint16_t>(interval->value);
      settings.normalize();
    }
    requestUpdate();
  });
}

void SpeedReaderSettingsActivity::selectCurrent() {
  switch (static_cast<Row>(selectedIndex)) {
    case Row::Enabled:
      settings.enabled = !settings.enabled;
      requestUpdate();
      break;
    case Row::WordsPerGroup:
      openWordsPicker();
      break;
    case Row::IntervalTenths:
      openIntervalPicker();
      break;
    case Row::Count:
      break;
  }
}

void SpeedReaderSettingsActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<SpeedReaderSettingsActivity*>(user);
  if (event.value < 0 || event.value >= static_cast<int>(kRowCount)) return;
  self->selectedIndex = event.value;
  self->ui.app.clearTapFlash();
  self->selectCurrent();
}

void SpeedReaderSettingsActivity::loop() {
  // Back keeps the edits: this screen has no separate save step, like the other reader options.
  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer) ||
      mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasHomeGesture()) {
    finishWithSettings();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    selectCurrent();
    return;
  }

  if (ui.routingReady()) {
    fui::ActionEvent event{};
    if (ui.routeTouch(mappedInput, event)) {
      if (ui.app.invalidated()) requestUpdate();
      if (event) return;
    }
  }

  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(kRowCount));
    topIndex = followListSelection(selectedIndex, topIndex, visibleRows, static_cast<int>(kRowCount));
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(kRowCount));
    topIndex = followListSelection(selectedIndex, topIndex, visibleRows, static_cast<int>(kRowCount));
    requestUpdate();
  });
}

void SpeedReaderSettingsActivity::listScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<SpeedReaderSettingsActivity*>(user)->buildListScreen(screen);
}

void SpeedReaderSettingsActivity::buildListScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + listHeaderHeight + metrics.verticalSpacing;
  const int noteReserve = kNoteMaxLines * renderer.getLineHeight(SMALL_FONT_ID) + metrics.verticalSpacing;
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(contentTop), 0,
                  static_cast<int16_t>(metrics.buttonHintsHeight + metrics.verticalSpacing * 2 + noteReserve), 0});

  refreshListItems();
  fui::ListProps props;
  props.items = listItems.data();
  props.count = static_cast<uint16_t>(kRowCount);
  props.selectedIndex = selectedIndex;
  props.topIndex = topIndex;
  props.action = kActionRow;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.valueText = screen.theme().smallText;
  visibleRows = std::max(1, static_cast<int>(configureUiList(props, screen.theme(), screen.body())));
  topIndex = scrollListBy(topIndex, 0, visibleRows, static_cast<int>(kRowCount));
  props.topIndex = topIndex;
  screen.list(props);
}

void SpeedReaderSettingsActivity::refreshListItems() {
  snprintf(wordsValue.data(), wordsValue.size(), "%u", static_cast<unsigned>(settings.wordsPerGroup));
  formatIntervalTenths(settings.intervalTenths, intervalValue.data(), intervalValue.size());

  const StrId labels[kRowCount] = {StrId::STR_SPEED_READER, StrId::STR_SPEED_READER_WORDS_PER_GROUP,
                                   StrId::STR_SPEED_READER_INTERVAL};
  const char* values[kRowCount] = {I18N.get(settings.enabled ? StrId::STR_ON : StrId::STR_OFF), wordsValue.data(),
                                   intervalValue.data()};
  for (size_t index = 0; index < kRowCount; ++index) {
    listItems[index] = fui::ListItem{};
    listItems[index].label = I18N.get(labels[index]);
    listItems[index].value = values[index];
    listItems[index].actionValue = static_cast<int16_t>(index);
  }
}

void SpeedReaderSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const Rect header{0, metrics.topPadding, pageWidth, metrics.headerHeight};
  listHeaderHeight = metrics.headerHeight;
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, header, tr(STR_SPEED_READER), true);
  } else {
    GUI.drawHeader(renderer, header, tr(STR_SPEED_READER));
  }

  ui.render();

  // The panel, not the setting, sets the real lower limit on time per group.
  const int noteWidth = pageWidth - metrics.contentSidePadding * 2;
  const auto noteLines = renderer.wrappedText(SMALL_FONT_ID, tr(STR_SPEED_READER_PANEL_NOTE), noteWidth,
                                              kNoteMaxLines, EpdFontFamily::REGULAR);
  const int noteLineHeight = renderer.getLineHeight(SMALL_FONT_ID);
  int noteY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing -
              static_cast<int>(noteLines.size()) * noteLineHeight;
  for (const auto& line : noteLines) {
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, noteY, line.c_str(), true);
    noteY += noteLineHeight;
  }

  const auto labels =
      mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
