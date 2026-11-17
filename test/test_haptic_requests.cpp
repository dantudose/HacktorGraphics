// Native: c++ -std=c++11 -Wall -Wextra -Werror -Iinclude
//         test/test_haptic_requests.cpp -o /tmp/hacktor-haptic-requests-tests
#include "HapticRequests.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

using watch::HapticPlayback;
using watch::HapticRequests;

void bootAndIdleHaveNoRequest() {
  HapticRequests requests;
  assert(!requests.pending() && !requests.timerPending());
  assert(requests.take(0, true) == HapticPlayback::None);
  assert(requests.take(100000, false) == HapticPlayback::None);
  requests.expireConfirmation(100000, false);
  requests.cancelConfirmation();
  requests.mute();
  assert(!requests.pending());
}

void eachRequestIsConsumedExactlyOnce() {
  HapticRequests requests;
  requests.requestConfirmation(1000);
  assert(requests.pending() && !requests.timerPending());
  assert(requests.take(1000, true) == HapticPlayback::Confirmation);
  assert(!requests.pending());
  assert(requests.take(1000, true) == HapticPlayback::None);
  requests.requestTimer();
  assert(requests.pending() && requests.timerPending());
  assert(requests.take(2000, true) == HapticPlayback::Timer);
  assert(!requests.pending() && !requests.timerPending());
  assert(requests.take(2000, true) == HapticPlayback::None);
}

void repeatedRequestsRemainBoundedToOne() {
  HapticRequests requests;
  requests.requestTimer();
  requests.requestTimer();
  requests.requestTimer();
  assert(requests.take(0, true) == HapticPlayback::Timer);
  assert(requests.take(0, true) == HapticPlayback::None);
  requests.requestConfirmation(100);
  requests.requestConfirmation(200);
  requests.requestConfirmation(300);
  assert(requests.take(300, true) == HapticPlayback::Confirmation);
  assert(requests.take(300, true) == HapticPlayback::None);
}

void timerDropsAndPreventsQueuedConfirmations() {
  HapticRequests requests;
  requests.requestConfirmation(100);
  requests.requestTimer();
  requests.requestConfirmation(200); // A pending timer cannot be replaced.
  assert(requests.timerPending());
  assert(requests.take(200, true) == HapticPlayback::Timer);
  assert(requests.take(200, true) == HapticPlayback::None);
  requests.requestConfirmation(300); // A later explicit action can queue anew.
  assert(requests.take(300, true) == HapticPlayback::Confirmation);
}

void rapidEnableDisableCancelsTheConfirmation() {
  HapticRequests requests;
  requests.requestConfirmation(100);
  requests.cancelConfirmation();
  assert(!requests.pending());
  assert(requests.take(101, true) == HapticPlayback::None);
  requests.requestConfirmation(102);
  requests.mute();
  assert(requests.take(103, true) == HapticPlayback::None);
  requests.requestConfirmation(104);
  assert(requests.take(104, true) == HapticPlayback::Confirmation);
}

void muteDropsEveryKindButCancelOnlyDropsConfirmation() {
  HapticRequests requests;
  requests.requestTimer();
  requests.cancelConfirmation();
  assert(requests.timerPending());
  requests.mute();
  assert(!requests.pending() && !requests.timerPending());
  assert(requests.take(0, true) == HapticPlayback::None);
  requests.requestConfirmation(10);
  requests.mute();
  assert(!requests.pending());
  assert(requests.take(10, true) == HapticPlayback::None);
}

void confirmationExpiresAtOneSecondExactly() {
  HapticRequests fresh;
  fresh.requestConfirmation(1000);
  fresh.expireConfirmation(1999, true);
  assert(fresh.pending());
  assert(fresh.take(1999, true) == HapticPlayback::Confirmation);

  HapticRequests expired;
  expired.requestConfirmation(1000);
  assert(expired.take(2000, true) == HapticPlayback::None);
  assert(!expired.pending());
  expired.requestConfirmation(3000);
  expired.expireConfirmation(4000, true);
  assert(!expired.pending());
  assert(expired.take(4000, true) == HapticPlayback::None);
}

void replacingAConfirmationRefreshesItsAgeWithoutQueuingMore() {
  HapticRequests requests;
  requests.requestConfirmation(100);
  requests.requestConfirmation(1000);
  requests.expireConfirmation(1100, true); // Expired relative to the old action.
  assert(requests.pending());
  assert(requests.take(1999, true) == HapticPlayback::Confirmation);
  assert(requests.take(1999, true) == HapticPlayback::None);
}

void screenOffDropsConfirmationPermanently() {
  HapticRequests requests;
  requests.requestConfirmation(100);
  requests.expireConfirmation(101, false);
  assert(!requests.pending());
  assert(requests.take(102, true) == HapticPlayback::None);
  requests.requestConfirmation(200);
  assert(requests.take(200, false) == HapticPlayback::None);
  assert(requests.take(201, true) == HapticPlayback::None);
}

void timerStillDeliversWhileAsleepAndAfterLongDelays() {
  HapticRequests requests;
  requests.requestConfirmation(0);
  requests.requestTimer();
  requests.expireConfirmation(100000, false);
  assert(requests.timerPending());
  assert(requests.take(100000, false) == HapticPlayback::Timer);
  assert(requests.take(100000, false) == HapticPlayback::None);
}

void confirmationAgeSurvivesMillisRollover() {
  const uint32_t since = std::numeric_limits<uint32_t>::max() - 499;
  HapticRequests fresh;
  fresh.requestConfirmation(since);
  assert(fresh.take(499, true) == HapticPlayback::Confirmation); // Age 999 ms.
  HapticRequests expired;
  expired.requestConfirmation(since);
  assert(expired.take(500, true) == HapticPlayback::None); // Age 1000 ms.
  assert(!expired.pending());
}

}  // namespace

int main() {
  bootAndIdleHaveNoRequest();
  eachRequestIsConsumedExactlyOnce();
  repeatedRequestsRemainBoundedToOne();
  timerDropsAndPreventsQueuedConfirmations();
  rapidEnableDisableCancelsTheConfirmation();
  muteDropsEveryKindButCancelOnlyDropsConfirmation();
  confirmationExpiresAtOneSecondExactly();
  replacingAConfirmationRefreshesItsAgeWithoutQueuingMore();
  screenOffDropsConfirmationPermanently();
  timerStillDeliversWhileAsleepAndAfterLongDelays();
  confirmationAgeSurvivesMillisRollover();
  std::puts("Haptic requests: all 11 queue scenarios passed.");
}
