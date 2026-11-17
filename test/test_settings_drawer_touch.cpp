#include "SettingsMenu.hpp"

#include <cassert>
#include <cstdio>

using watch::SettingsAction;
using watch::SettingsData;
using watch::SettingsEvent;
using watch::SettingsMenu;
using watch::SettingsPage;

namespace {

SettingsData data;

void noDrawer(const SettingsEvent& event) {
  assert(!event.drawer_dragging && !event.drawer_released);
  assert(event.drawer_offset == 0);
}

void ready(SettingsMenu& menu) {
  menu.open();
  noDrawer(menu.update(data, false, 0, 0, 0));
}

void tap(SettingsMenu& menu, int x, int y) {
  noDrawer(menu.update(data, true, x, y, 10));
  noDrawer(menu.update(data, false, 0, 0, 20));
}

void date(SettingsMenu& menu) {
  tap(menu, 120, 204);
  tap(menu, 120, 174);
  assert(menu.page() == SettingsPage::Date);
}

void upwardIntentReportsProgressAndRelease() {
  SettingsMenu menu;
  ready(menu);
  noDrawer(menu.update(data, true, 120, 204, 100));
  noDrawer(menu.update(data, true, 120, 193, 120));
  auto event = menu.update(data, true, 126, 192, 140);
  assert(event.drawer_dragging && event.drawer_offset == -12);
  assert(!event.drawer_released && event.action == SettingsAction::None);
  event = menu.update(data, true, 120, 140, 160);
  assert(event.drawer_dragging && event.drawer_offset == -64);
  event = menu.update(data, false, 0, 0, 180);
  assert(!event.drawer_dragging && event.drawer_released);
  assert(event.drawer_offset == -64 && event.action == SettingsAction::Close);
  assert(menu.isOpen() && menu.page() == SettingsPage::Quick);
}

void shortUpwardPullReleasesWithoutClosing() {
  SettingsMenu menu;
  ready(menu);
  menu.update(data, true, 120, 204, 100);
  menu.update(data, true, 120, 160, 140);
  const auto event = menu.update(data, false, 0, 0, 180);
  assert(event.drawer_released && event.drawer_offset == -44);
  assert(event.action == SettingsAction::None && menu.isOpen());
  assert(menu.page() == SettingsPage::Quick);
}

void finalGeometryCanCancelAClaimedPull() {
  SettingsMenu menu;
  ready(menu);
  menu.update(data, true, 120, 204, 100);
  menu.update(data, true, 120, 184, 120);
  auto event = menu.update(data, true, 160, 144, 140);
  assert(event.drawer_dragging && event.drawer_offset == -60);
  event = menu.update(data, false, 0, 0, 160);
  assert(event.drawer_released && event.drawer_offset == -60);
  assert(event.action == SettingsAction::None && menu.isOpen());
}

void reversalClampsAtOriginWithoutActivatingTheInitialButton() {
  SettingsMenu menu;
  ready(menu);
  menu.update(data, true, 80, 88, 100);
  menu.update(data, true, 80, 68, 120);
  auto event = menu.update(data, true, 160, 98, 140);
  assert(event.drawer_dragging && event.drawer_offset == 0);
  event = menu.update(data, true, 80, 88, 160);
  assert(event.drawer_dragging && event.drawer_offset == 0);
  event = menu.update(data, false, 0, 0, 180);
  assert(event.drawer_released && event.drawer_offset == 0);
  assert(event.action == SettingsAction::None);
}

void reversalCanTrackUpwardAgain() {
  SettingsMenu menu;
  ready(menu);
  menu.update(data, true, 120, 170, 100);
  menu.update(data, true, 120, 150, 120);
  menu.update(data, true, 120, 190, 140);
  auto event = menu.update(data, true, 120, 110, 160);
  assert(event.drawer_dragging && event.drawer_offset == -60);
  event = menu.update(data, false, 0, 0, 180);
  assert(event.drawer_released && event.action == SettingsAction::Close);
}

void horizontalFirstCannotTurnIntoDismissal() {
  SettingsMenu menu;
  ready(menu);
  menu.update(data, true, 100, 160, 100);
  noDrawer(menu.update(data, true, 112, 154, 120));
  noDrawer(menu.update(data, true, 100, 70, 140));
  const auto event = menu.update(data, false, 0, 0, 160);
  noDrawer(event);
  assert(event.action == SettingsAction::None && menu.isOpen());
}

void ambiguousDiagonalWaitsForAnUpwardIntent() {
  SettingsMenu menu;
  ready(menu);
  menu.update(data, true, 100, 160, 100);
  noDrawer(menu.update(data, true, 113, 146, 120));
  const auto event = menu.update(data, true, 113, 130, 140);
  assert(event.drawer_dragging && event.drawer_offset == -30);
}

void verticalSliderDriftNeverBecomesDismissal() {
  SettingsMenu menu;
  ready(menu);
  tap(menu, 160, 152);
  assert(menu.page() == SettingsPage::Brightness);
  auto event = menu.update(data, true, 120, 126, 100);
  noDrawer(event);
  assert(event.action == SettingsAction::BrightnessPreview && event.value == 50);
  noDrawer(menu.update(data, true, 120, 20, 120));
  event = menu.update(data, false, 0, 0, 140);
  noDrawer(event);
  assert(event.action == SettingsAction::BrightnessCommit && event.value == 50);
  assert(menu.isOpen());
}

void repeatedAdjustmentOwnsTheContactUntilLift() {
  SettingsMenu menu;
  ready(menu);
  date(menu);
  const int year = menu.editedTime().year;
  menu.update(data, true, 160, 153, 1000);
  auto event = menu.update(data, true, 160, 153, 1500);
  noDrawer(event);
  assert(event.redraw && menu.editedTime().year == year + 1);
  noDrawer(menu.update(data, true, 160, 80, 1520));
  event = menu.update(data, false, 0, 0, 1540);
  noDrawer(event);
  assert(event.action == SettingsAction::None);
  assert(menu.editedTime().year == year + 1 && menu.isOpen());
}

void upwardIntentBeforeAnAdjustmentHoldWins() {
  SettingsMenu menu;
  ready(menu);
  date(menu);
  const int year = menu.editedTime().year;
  menu.update(data, true, 160, 153, 1000);
  auto event = menu.update(data, true, 160, 130, 1500);
  assert(event.drawer_dragging && event.drawer_offset == -23);
  assert(!event.redraw && menu.editedTime().year == year);
  event = menu.update(data, false, 0, 0, 1520);
  assert(event.drawer_released && event.action == SettingsAction::None);
  assert(menu.editedTime().year == year);
}

void resetCancelsProgressUntilAFreshLift() {
  SettingsMenu menu;
  ready(menu);
  menu.update(data, true, 120, 204, 100);
  assert(menu.update(data, true, 120, 150, 120).drawer_dragging);
  menu.resetTouch();
  noDrawer(menu.update(data, true, 120, 120, 140));
  noDrawer(menu.update(data, false, 0, 0, 160));
  menu.update(data, true, 120, 204, 180);
  assert(menu.update(data, true, 120, 150, 200).drawer_dragging);
}

void invalidCoordinatesCancelProgressUntilLift() {
  SettingsMenu menu;
  ready(menu);
  menu.update(data, true, 120, 204, 100);
  assert(menu.update(data, true, 120, 150, 120).drawer_dragging);
  noDrawer(menu.update(data, true, 240, 120, 140));
  noDrawer(menu.update(data, true, 120, 100, 160));
  noDrawer(menu.update(data, false, 0, 0, 180));
  assert(menu.isOpen());
}

void releaseIsReportedOnceAndRearmsTheNextContact() {
  SettingsMenu menu;
  ready(menu);
  menu.update(data, true, 120, 204, 100);
  menu.update(data, true, 120, 175, 120);
  assert(menu.update(data, false, 0, 0, 140).drawer_released);
  noDrawer(menu.update(data, false, 0, 0, 160));
  menu.update(data, true, 80, 88, 180);
  const auto event = menu.update(data, false, 0, 0, 200);
  noDrawer(event);
  assert(event.action == SettingsAction::ToggleAirplane);
}

void anExistingFingerAfterOpeningCannotDismiss() {
  SettingsMenu menu;
  menu.open();
  noDrawer(menu.update(data, true, 120, 204, 100));
  noDrawer(menu.update(data, true, 120, 120, 120));
  noDrawer(menu.update(data, false, 0, 0, 140));
}

void dismissalIsAvailableOnEverySettingsPage() {
  for (int page = 0; page < 5; ++page) {
    SettingsMenu menu;
    ready(menu);
    if (page == 1) tap(menu, 160, 152);
    if (page >= 2) tap(menu, 120, 204);
    if (page >= 3) tap(menu, 120, 174);
    if (page == 4) tap(menu, 120, 201);
    assert(static_cast<int>(menu.page()) == page);
    menu.update(data, true, 120, 204, 100);
    assert(menu.update(data, true, 120, 140, 120).drawer_dragging);
    const auto event = menu.update(data, false, 0, 0, 140);
    assert(event.drawer_released && event.action == SettingsAction::Close);
    assert(static_cast<int>(menu.page()) == page);
  }
}

void aClosedMenuCannotEmitDrawerEvents() {
  SettingsMenu menu;
  ready(menu);
  menu.update(data, true, 120, 204, 100);
  menu.update(data, true, 120, 150, 120);
  menu.close();
  noDrawer(menu.update(data, true, 120, 100, 140));
  noDrawer(menu.update(data, false, 0, 0, 160));
}

}  // namespace

int main() {
  upwardIntentReportsProgressAndRelease();
  shortUpwardPullReleasesWithoutClosing();
  finalGeometryCanCancelAClaimedPull();
  reversalClampsAtOriginWithoutActivatingTheInitialButton();
  reversalCanTrackUpwardAgain();
  horizontalFirstCannotTurnIntoDismissal();
  ambiguousDiagonalWaitsForAnUpwardIntent();
  verticalSliderDriftNeverBecomesDismissal();
  repeatedAdjustmentOwnsTheContactUntilLift();
  upwardIntentBeforeAnAdjustmentHoldWins();
  resetCancelsProgressUntilAFreshLift();
  invalidCoordinatesCancelProgressUntilLift();
  releaseIsReportedOnceAndRearmsTheNextContact();
  anExistingFingerAfterOpeningCannotDismiss();
  dismissalIsAvailableOnEverySettingsPage();
  aClosedMenuCannotEmitDrawerEvents();
  std::puts("Settings drawer touch: all 16 scenarios passed.");
}
