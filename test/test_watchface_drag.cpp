#include "WatchfaceSlide.hpp"
#include "WatchfaceTouch.hpp"

#include <cassert>
#include <cstdio>

using watch::SwipeDirection;
using watch::TimerAction;
using watch::TimerUiState;
using watch::WatchfaceSlide;
using watch::WatchfaceTouch;

namespace {

void heldTimerDragSettlesFromTheLivePosition() {
  WatchfaceTouch touch;
  WatchfaceSlide slide;
  touch.update(true, TimerUiState::Ready, false, 0, 0, 0);
  touch.update(true, TimerUiState::Ready, true, 180, 209, 100);
  auto input = touch.update(true, TimerUiState::Ready, true, 130, 209, 200);
  assert(input.dragging && input.timer_action == TimerAction::None);
  slide.drag(input.drag_offset, 200);
  assert(slide.frame(200).outgoing_x == -50);
  slide.presented(200);

  input = touch.update(true, TimerUiState::Ready, true, 70, 209, 1200);
  assert(input.dragging && input.timer_action == TimerAction::None);
  slide.drag(input.drag_offset, 1200);
  assert(slide.frame(1200).outgoing_x == -110);
  slide.presented(1200);
  assert(!slide.due(10000));

  input = touch.update(true, TimerUiState::Ready, false, 0, 0, 10000);
  assert(input.drag_released && input.swipe == SwipeDirection::Left);
  assert(input.timer_action == TimerAction::None);
  slide.release(input.swipe != SwipeDirection::None, 10000);
  assert(slide.frame(10000).outgoing_x == -110);
  const auto end = slide.frame(10200);
  assert(end.finished && end.outgoing_x == -240 && end.incoming_x == 0);
}

void reversingTimerDragCanCancelWithoutActivatingStart() {
  WatchfaceTouch touch;
  WatchfaceSlide slide;
  touch.update(true, TimerUiState::Ready, false, 0, 0, 0);
  touch.update(true, TimerUiState::Ready, true, 120, 209, 100);
  auto input = touch.update(true, TimerUiState::Ready, true, 60, 209, 200);
  assert(input.dragging && input.timer_action == TimerAction::None);
  slide.drag(input.drag_offset, 200);
  assert(slide.frame(200).outgoing_x == -60);

  input = touch.update(true, TimerUiState::Ready, true, 120, 209, 300);
  slide.drag(input.drag_offset, 300);
  assert(slide.frame(300).outgoing_x == 0);
  input = touch.update(true, TimerUiState::Ready, true, 140, 209, 400);
  assert(input.dragging && input.timer_action == TimerAction::None);
  slide.drag(input.drag_offset, 400);
  assert(slide.frame(400).outgoing_x == 20);
  assert(slide.frame(400).incoming_x == -220);

  input = touch.update(true, TimerUiState::Ready, false, 0, 0, 500);
  assert(input.drag_released && input.swipe == SwipeDirection::None);
  assert(input.timer_action == TimerAction::None);
  slide.release(input.swipe != SwipeDirection::None, 500);
  assert(slide.frame(500).outgoing_x == 20);
  const auto end = slide.frame(700);
  assert(end.finished && end.outgoing_x == 0 && end.incoming_x == -240);
}

void adjustmentHoldCannotStartAPreviewAfterRepeating() {
  WatchfaceTouch touch;
  touch.update(true, TimerUiState::Ready, false, 0, 0, 0);
  touch.update(true, TimerUiState::Ready, true, 160, 165, 100);
  auto input = touch.update(true, TimerUiState::Ready, true, 160, 165, 600);
  assert(input.timer_action == TimerAction::Increment);
  assert(!input.dragging && !input.drag_released);
  input = touch.update(true, TimerUiState::Ready, true, 80, 165, 650);
  assert(!input.dragging && input.timer_action == TimerAction::None);
  input = touch.update(true, TimerUiState::Ready, false, 0, 0, 700);
  assert(!input.drag_released && input.swipe == SwipeDirection::None);
  assert(input.timer_action == TimerAction::None);
}

void canceledPreviewRequiresLiftBeforeOperatingControls() {
  WatchfaceTouch touch;
  WatchfaceSlide slide;
  touch.update(true, TimerUiState::Ready, false, 0, 0, 0);
  touch.update(true, TimerUiState::Ready, true, 180, 209, 100);
  auto input = touch.update(true, TimerUiState::Ready, true, 130, 209, 200);
  slide.drag(input.drag_offset, 200);
  assert(slide.dragging());

  // An external interruption (timer expiry, button, or touch transport error).
  touch.reset();
  slide.cancel();
  input = touch.update(true, TimerUiState::Ready, true, 120, 209, 300);
  assert(!input.dragging && input.timer_action == TimerAction::None);
  assert(!slide.active());
  input = touch.update(true, TimerUiState::Ready, false, 0, 0, 400);
  assert(!input.drag_released && input.timer_action == TimerAction::None);
  touch.update(true, TimerUiState::Ready, true, 120, 209, 500);
  input = touch.update(true, TimerUiState::Ready, false, 0, 0, 600);
  assert(input.timer_action == TimerAction::StartPause);
}

}  // namespace

int main() {
  heldTimerDragSettlesFromTheLivePosition();
  reversingTimerDragCanCancelWithoutActivatingStart();
  adjustmentHoldCannotStartAPreviewAfterRepeating();
  canceledPreviewRequiresLiftBeforeOperatingControls();
  std::puts("Watchface drag: all 4 combined scenarios passed.");
}
