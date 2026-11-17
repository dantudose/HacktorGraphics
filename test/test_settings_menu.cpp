#include "SettingsMenu.hpp"

#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <limits>

using watch::SettingsAction;
using watch::SettingsData;
using watch::SettingsEvent;
using watch::SettingsMenu;
using watch::SettingsPage;

namespace {

uint32_t now_ms = 0;

SettingsEvent tap(SettingsMenu& menu, const SettingsData& data, int x, int y) {
  now_ms += 100;
  assert(menu.update(data,true,x,y,now_ms).action == SettingsAction::None);
  now_ms += 50;
  return menu.update(data,false,0,0,now_ms);
}

void ready(SettingsMenu& menu, const SettingsData& data) {
  menu.open();
  assert(menu.update(data,false,0,0,now_ms).action == SettingsAction::None);
}

void more(SettingsMenu& menu, const SettingsData& data) {
  tap(menu,data,120,204);
  assert(menu.page() == SettingsPage::More);
}

void date(SettingsMenu& menu, const SettingsData& data) {
  more(menu,data);
  tap(menu,data,120,174);
  assert(menu.page() == SettingsPage::Date);
}

void brightnessHasANonzeroFloorAndFullRange() {
  watch::WatchSettings settings;
  settings.brightness = 0;
  assert(watch::brightnessDuty(settings) == 102);
  assert(watch::brightnessDuty(settings,1) == 1);
  settings.brightness = 50;
  assert(watch::brightnessDuty(settings) == 563);
  settings.brightness = 100;
  assert(watch::brightnessDuty(settings) == 1023);
  uint16_t previous = 0;
  for (unsigned value = 0; value <= 100; ++value) {
    settings.brightness = static_cast<uint8_t>(value);
    const uint16_t duty = watch::brightnessDuty(settings);
    assert(duty >= previous && duty >= 102 && duty <= 1023);
    previous = duty;
  }
}

void persistedValuesAreSanitized() {
  watch::WatchSettings a;
  watch::WatchSettings b = a;
  assert(a == b);
  b.airplane = true;
  assert(a != b);
  b.brightness = 255;
  b.timeout_seconds = 255;
  b = watch::sanitizeSettings(b);
  assert(b.brightness == 100 && b.timeout_seconds == 10);
  for (uint8_t seconds : {5,10,15,30}) {
    b.timeout_seconds = seconds;
    assert(watch::timeoutMillis(b) == uint32_t(seconds) * 1000U);
  }
}

void quickTilesEmitActionsOnlyOnRelease() {
  SettingsData data;
  SettingsMenu menu;
  ready(menu,data);
  assert(tap(menu,data,80,88).action == SettingsAction::ToggleAirplane);
  assert(tap(menu,data,160,88).action == SettingsAction::ToggleVibration);
  assert(tap(menu,data,80,152).action == SettingsAction::ToggleMotionWake);
  // The menu owns no settings mutation or persistence.
  assert(!data.values.airplane && data.values.vibration && data.values.motion_wake);
}

void openingRequiresAnExistingFingerToLift() {
  SettingsData data;
  SettingsMenu menu;
  menu.open();
  assert(menu.update(data,true,80,88,100).action == SettingsAction::None);
  assert(menu.update(data,true,80,88,200).action == SettingsAction::None);
  assert(menu.update(data,false,0,0,300).action == SettingsAction::None);
  assert(tap(menu,data,80,88).action == SettingsAction::ToggleAirplane);
}

void draggingBetweenButtonsDoesNotToggleEither() {
  SettingsData data;
  SettingsMenu menu;
  ready(menu,data);
  menu.update(data,true,80,88,100);
  menu.update(data,true,160,88,200);
  menu.update(data,true,80,88,300);
  assert(menu.update(data,false,0,0,400).action == SettingsAction::None);
}

void swipeUpDismissesInsteadOfTappingAButton() {
  SettingsData data;
  SettingsMenu menu;
  ready(menu,data);
  menu.update(data,true,120,204,100);
  menu.update(data,true,120,140,200);
  const auto event = menu.update(data,false,0,0,300);
  assert(event.action == SettingsAction::Close && event.redraw);
  assert(event.drawer_released && event.drawer_offset == -64);
  // The caller keeps rendering the menu until its closing animation finishes.
  assert(menu.isOpen());
  menu.close();
  assert(!menu.isOpen());
}

void horizontalAndDownwardDragsDoNotDismiss() {
  SettingsData data;
  SettingsMenu menu;
  ready(menu,data);
  menu.update(data,true,70,110,100);
  menu.update(data,true,170,110,200);
  assert(menu.update(data,false,0,0,300).action == SettingsAction::None);
  assert(menu.isOpen());
  menu.update(data,true,120,50,400);
  menu.update(data,true,120,110,500);
  assert(menu.update(data,false,0,0,600).action == SettingsAction::None);
  assert(menu.isOpen());
}

void sliderPreviewsThenCommitsOnLift() {
  SettingsData data;
  SettingsMenu menu;
  ready(menu,data);
  tap(menu,data,160,152);
  assert(menu.page() == SettingsPage::Brightness);
  auto event = menu.update(data,true,45,126,1000);
  assert(event.action == SettingsAction::BrightnessPreview && event.value == 0);
  assert(data.values.brightness == 100);
  event = menu.update(data,true,120,126,1020);
  assert(event.action == SettingsAction::BrightnessPreview && event.value == 50);
  event = menu.update(data,true,195,126,1040);
  assert(event.action == SettingsAction::BrightnessPreview && event.value == 100);
  assert(menu.update(data,true,195,126,1100).action == SettingsAction::None);
  event = menu.update(data,false,0,0,1120);
  assert(event.action == SettingsAction::BrightnessCommit && event.value == 100);
  assert(menu.isOpen());
}

void capturedSliderClampsAndDoesNotBecomeSwipeUp() {
  SettingsData data;
  SettingsMenu menu;
  ready(menu,data);
  tap(menu,data,160,152);
  menu.update(data,true,120,126,1000);
  auto event = menu.update(data,true,10,70,1020);
  assert(event.action == SettingsAction::BrightnessPreview && event.value == 0);
  event = menu.update(data,false,0,0,1040);
  assert(event.action == SettingsAction::BrightnessCommit && event.value == 0);
  assert(menu.isOpen());
}

void touchFailureCancelsUncommittedSlider() {
  SettingsData data;
  SettingsMenu menu;
  ready(menu,data);
  tap(menu,data,160,152);
  menu.update(data,true,45,126,1000);
  menu.resetTouch();
  assert(menu.update(data,true,195,126,1100).action == SettingsAction::None);
  assert(menu.update(data,false,0,0,1200).action == SettingsAction::None);
}

void moreControlsAndSyncAvailabilityAreHonored() {
  SettingsData data;
  SettingsMenu menu;
  ready(menu,data);
  more(menu,data);
  assert(tap(menu,data,120,82).action == SettingsAction::NextTimeout);
  data.sync_available = false;
  assert(tap(menu,data,120,127).action == SettingsAction::None);
  data.sync_available = true;
  assert(tap(menu,data,120,127).action == SettingsAction::SyncTime);
  menu.update(data,true,120,127,2000);
  data.sync_available = false;
  assert(menu.update(data,false,0,0,2100).action == SettingsAction::None);
}

void dateChangesClampLeapDaysAndOnlySaveApplies() {
  SettingsData data;
  data.clock = watch::WatchDateTime(2024,1,31,23,59,45);
  SettingsMenu menu;
  ready(menu,data);
  date(menu,data);
  tap(menu,data,128,94); // Month.
  assert(tap(menu,data,160,153).action == SettingsAction::None);
  assert(menu.editedTime().month == 2 && menu.editedTime().day == 29);
  tap(menu,data,61,94); // Year.
  tap(menu,data,160,153);
  assert(menu.editedTime().year == 2025 && menu.editedTime().day == 28);
  assert(tap(menu,data,120,201).action == SettingsAction::None);
  assert(menu.page() == SettingsPage::Time);
  tap(menu,data,162,94); // Minute.
  tap(menu,data,80,153);
  const auto event = tap(menu,data,120,201);
  assert(event.action == SettingsAction::ApplyTime);
  assert(menu.page() == SettingsPage::More);
  const auto& edited = menu.editedTime();
  assert(edited.year == 2025 && edited.month == 2 && edited.day == 28);
  assert(edited.hour == 23 && edited.minute == 58 && edited.second == 0);
  assert(data.clock.year == 2024 && data.clock.month == 1 && data.clock.day == 31);
}

void dateBackNavigationNeverAppliesTheDraft() {
  SettingsData data;
  SettingsMenu menu;
  ready(menu,data);
  date(menu,data);
  tap(menu,data,120,201);
  assert(menu.page() == SettingsPage::Time);
  assert(tap(menu,data,60,37).action == SettingsAction::None);
  assert(menu.page() == SettingsPage::Date);
  assert(tap(menu,data,60,37).action == SettingsAction::None);
  assert(menu.page() == SettingsPage::More);
  assert(tap(menu,data,60,37).action == SettingsAction::None);
  assert(menu.page() == SettingsPage::Quick);
}

void dateAdjustmentHoldRepeatsWithoutReleaseIncrement() {
  SettingsData data;
  data.clock = watch::WatchDateTime(2026,11,5);
  SettingsMenu menu;
  ready(menu,data);
  date(menu,data);
  menu.update(data,true,160,153,1000);
  assert(!menu.update(data,true,160,153,1499).redraw);
  assert(menu.update(data,true,160,153,1500).redraw);
  assert(menu.editedTime().year == 2027);
  assert(!menu.update(data,true,160,153,1619).redraw);
  assert(menu.update(data,true,160,153,1620).redraw);
  assert(menu.editedTime().year == 2028);
  assert(menu.update(data,false,0,0,1700).action == SettingsAction::None);
  assert(menu.editedTime().year == 2028);
}

void adjustmentHoldSurvivesMillisRollover() {
  SettingsData data;
  data.clock = watch::WatchDateTime(2026,11,5);
  SettingsMenu menu;
  ready(menu,data);
  date(menu,data);
  const uint32_t start = std::numeric_limits<uint32_t>::max() - 199U;
  menu.update(data,true,160,153,start);
  assert(!menu.update(data,true,160,153,299).redraw);
  assert(menu.update(data,true,160,153,300).redraw);
  assert(menu.editedTime().year == 2027);
}

void clockValidationHandlesGregorianCenturiesAndBounds() {
  assert(watch::validDateTime(watch::WatchDateTime(2000,2,29)));
  assert(!watch::validDateTime(watch::WatchDateTime(1900,2,29)));
  assert(!watch::validDateTime(watch::WatchDateTime(0,1,1)));
  assert(!watch::validDateTime(watch::WatchDateTime(2026,1,1,24)));
  const auto repaired = watch::sanitizeDateTime(watch::WatchDateTime(10000,13,32,25,70,70));
  assert(watch::validDateTime(repaired));
  assert(repaired.year == 9999 && repaired.month == 12 && repaired.day == 31);
}

void screenBoundsAndClosedMenuCannotActivateControls() {
  SettingsData data;
  SettingsMenu menu;
  assert(menu.update(data,true,80,88,100).action == SettingsAction::None);
  ready(menu,data);
  assert(tap(menu,data,0,0).action == SettingsAction::None);
  menu.update(data,true,80,88,1000);
  menu.update(data,true,240,120,1100);
  assert(menu.update(data,false,0,0,1200).action == SettingsAction::None);
  menu.close();
  assert(tap(menu,data,80,88).action == SettingsAction::None);
}

}  // namespace

int main() {
  brightnessHasANonzeroFloorAndFullRange();
  persistedValuesAreSanitized();
  quickTilesEmitActionsOnlyOnRelease();
  openingRequiresAnExistingFingerToLift();
  draggingBetweenButtonsDoesNotToggleEither();
  swipeUpDismissesInsteadOfTappingAButton();
  horizontalAndDownwardDragsDoNotDismiss();
  sliderPreviewsThenCommitsOnLift();
  capturedSliderClampsAndDoesNotBecomeSwipeUp();
  touchFailureCancelsUncommittedSlider();
  moreControlsAndSyncAvailabilityAreHonored();
  dateChangesClampLeapDaysAndOnlySaveApplies();
  dateBackNavigationNeverAppliesTheDraft();
  dateAdjustmentHoldRepeatsWithoutReleaseIncrement();
  adjustmentHoldSurvivesMillisRollover();
  clockValidationHandlesGregorianCenturiesAndBounds();
  screenBoundsAndClosedMenuCannotActivateControls();
  std::puts("Settings menu: all 17 scenarios passed.");
}
