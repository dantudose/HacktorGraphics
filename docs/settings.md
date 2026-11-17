# Quick settings

With a watchface lit, swipe down starting in the upper 50 pixels. Once vertical
intent locks at 12 pixels, Settings follows the finger from the top while the
selected watchface moves down. Release with at least 45 pixels of downward
travel and twice the horizontal travel to open it; shorter or canceled pulls
return to the watchface. The gesture owns its contact, so it cannot also switch
faces or activate timer controls. Horizontal drags keep the carousel behavior.

Swipe up to close Settings with the same finger-following motion. Release after
45 pixels of upward travel to dismiss it; otherwise it returns to its open
position. Horizontal brightness adjustment and a date/time adjustment hold
retain their own contact and cannot also dismiss the panel. You can hold a
drawer partly open, reverse its movement, or return to the start before lifting.
Buttons receive fresh touches only once the panel has settled.

Completion and snap-back ease from the current position over 60–200 ms. A
stationary held panel does not repeatedly redraw. Pressing the button interrupts
a live drag at its starting view or a released transition at its selected
destination. The normal screen timeout closes Settings. Holding the button still
opens Debug. A touch transport error or timer expiry cancels a live drag and
restores its starting view without selecting a different watchface.

Battery percentage and charging state appear above four tiles:

| Tile | Effect |
| --- | --- |
| Airplane | Stops BLE activity advertising/connections and suppresses startup, scheduled, retry, and manual Bluetooth time sync. |
| Vibration | Mutes queued and active timer vibration. The timer still completes and resets. |
| Wrist wake | Enables/disables motion-triggered display and processor wake. Button wake and step counting continue. |
| Brightness | Opens a slider labeled 0–100. |

Turning Vibration from off to on gives one **500 ms confirmation buzz**. Loading
an enabled preference at startup does not vibrate. Turning it off cancels queued
feedback and stops active playback. A timer alert takes priority, so its six
pulses cannot be replaced by settings feedback. Queued feedback expires after
one second or if the screen goes off; it cannot play unexpectedly on a later wake.
The motor uses the existing bounded poll/settle and IMU-motion suppression, with
no new standby polling or changes to the selected face or screen state.

The quick tiles use a diagonal airplane silhouette, a watch with a curved raise
arrow, and matching rounded vibration/brightness icons. RGB565 uses smoothed
strokes; the palette fallback uses solid strokes with the same geometry. These
are drawn directly onto the shared canvas, without extra sprites or icon fonts.

Brightness maps linearly from **10% PWM at slider 0** to **100% PWM at slider
100**. It never switches the screen off while adjusting. The reversed backlight
MOSFET still limits the physical dimming range. Normal timeout/fade and full
peripheral-rail shutdown remain available independently of this setting.

The More page contains:

- Screen timeout: tap to cycle 5, 10, 15, and 30 seconds, plus the existing fade.
- Sync time now: shows the last successful sync and the attempt's result.
- Set date/time: select year/month/day, adjust with −/+, tap Next, then edit
  hour/minute and Save. Hold −/+ to repeat. Gregorian leap days are validated;
  changing month/year clamps the day as needed. Saving sets seconds to zero.
- BLE advertising: defaults to On. Tap to enable or disable three-second
  discovery windows every five seconds, including while the display is asleep. A
  connected phone gets up to 30 seconds for inspection before disconnection.
  The choice is saved independently of CTS; Airplane mode suppresses both uses.

Manual sync shows `Syncing...` before starting the existing synchronous BLE
operation. It is unavailable in airplane mode, during a running timer or motor
alert, while a phone occupies the BLE connection slot, while the activity
peripheral is shutting down, and shortly before the midnight step reset; the
menu shows the reason.
Manual connections use a five-second connect timeout and no connect retries.
The interface waits for the attempt, including characteristic discovery/read.
The [activity peripheral](ble-activity.md) and CTS client share one radio and
one connection slot. CTS waits for an attached phone to disconnect rather than
interrupting it. With no phone attached, manual CTS can reuse the stack and
release only its own client, preserving the activity service. An explicit
manual attempt can extend the active discovery window while it blocks the main
loop. Automatic CTS runs between discovery windows instead. Disabling BLE
advertising does not disable time synchronization.

Manual date/time changes affect the running watch clock and daily step date.
Same-date changes preserve steps; another retained date restores its total and
starts a fresh sensor baseline. A date with no record starts at zero. Backward
corrections within the retained fortnight preserve newer records; correcting
more than 14 days behind the newest record starts a new history window.
When airplane mode is off, a later CTS sync can replace the manual clock value.
A valid step-history checkpoint supplies the saved clock date/time at startup,
with compile-time fallback only when history is missing or invalid. There is no
persistent RTC to recover elapsed time across reset/power loss. Manual clock
values are included only when a changed step-history checkpoint is saved;
unchanged totals do not trigger timestamp-only writes.

## Persistence and power

The six preference values survive restarts in NVS. Only changed keys are saved.
Brightness is previewed live and saved on finger release; an interrupted edit
reverts to the last saved value. There are no repeated saves during dragging.
Date/time editing uses a draft until Save, so leaving the editor cancels it.
[Steps and estimated distance](daily-steps.md) use a separate 30-minute
checkpoint, saving only changed dated totals and retaining today plus 14 earlier
calendar dates. Clock edits do not force additional history writes. A reset
normally loses only the most recent 30 minutes of activity; failed sensor reads,
failed writes, or delayed service can extend that interval.

With Airplane mode off and BLE advertising On,
[activity inspection](ble-activity.md) exposes read-only daily steps and
estimated distance to nRF Connect during periodic discovery and bounded phone
sessions. The screen does not control their schedule. BLE reads do not prolong
the screen deadline, wake the display, force an IMU read, or save flash.
Dark reads can return the cached total
from the last visible refresh, midnight snapshot, or 30-minute checkpoint.
The current SDK has automatic power management and Bluetooth modem sleep
disabled, so the processor remains awake during each three-second discovery
window and connected session. BLE shuts down between windows to allow normal
processor light sleep. Without a phone, about three awake seconds per five-second
cycle leave roughly two seconds for light sleep minus setup/cleanup overhead;
this increases battery load compared with minute-spaced windows. Setting this
toggle Off removes discovery wakes entirely.
Battery impact still needs hardware measurement.

The menu shares the existing canvas, redraws when its state/data changes, and
adds no frame buffer, touch wake source, or standby polling. Airplane mode also
removes disabled sync deadlines from sleep scheduling. Wrist wake off excludes
IMU wake levels without disabling the accelerometer/pedometer. Midnight step
reset, 30-minute step checkpoints, and timer deadlines remain scheduled normally.
During drawer motion, each view is composed in turn and only its visible area
is transferred opaquely at its vertical offset. The transfer finishes before the
canvas is reused. Frames run through the main loop so timer/vibration services
continue between them. Touch and BLE sync cannot activate moving menu controls;
display sleep cancels the transition and parks peripherals normally.

## Debug power statistics

The long-press debug screen now shows uptime, screen-on minutes/seconds and its
percentage of uptime, successful processor light-sleep time, total screen wakes
and button/motion wake counts, followed by steps, battery, last sync, resets and
the selected face. `B` means button and `M` means motion. The total includes
boot/other wakes as well. Measurements start over at reboot and are updated by
events, without polling or flash writes.

Brightness/fade updates do not inflate the wake count. The brief reset-held
backlight glow during timer alerts, midnight sensor service, or 30-minute step
checkpoints does not count as normal screen-on time. These are activity
statistics, not current measurements
or a battery-life prediction. A connected USB host still prevents processor
light sleep to preserve upload/console access.

## Validation

Native tests cover menu ownership, brightness mapping and commit behavior,
manual clock editing, persistence failures/retries, and power accounting:

```sh
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_settings_menu.cpp -o /tmp/hacktor-settings-menu-tests
/tmp/hacktor-settings-menu-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_settings_pull_down.cpp -o /tmp/hacktor-settings-pull-down-tests
/tmp/hacktor-settings-pull-down-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_settings_slide.cpp -o /tmp/hacktor-settings-slide-tests
/tmp/hacktor-settings-slide-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_settings_drawer_touch.cpp -o /tmp/hacktor-settings-drawer-touch-tests
/tmp/hacktor-settings-drawer-touch-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_settings_store.cpp -o /tmp/hacktor-settings-store-tests
/tmp/hacktor-settings-store-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_power_stats.cpp -o /tmp/hacktor-power-stats-tests
/tmp/hacktor-power-stats-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_haptic_requests.cpp -o /tmp/hacktor-haptic-requests-tests
/tmp/hacktor-haptic-requests-tests
pio run -e hacktor_watch_s3_lgfx
```

On hardware, open from every face, including Timer; check that opening/closing
cannot activate another control. Verify slider endpoints remain visible, timeout
and fade, persisted preferences after reboot, motion-off button wake and step
counting, muted timer completion, and both clock-edit and sync result paths.
Enable vibration and check one half-second buzz; disable it during playback and
toggle rapidly to verify cancellation. Reboot with vibration enabled and expect
no confirmation. Let a timer expire during confirmation and verify that all six
timer pulses follow without a long standby delay. Check the new icons on/off.
Observe debug screen time/wake counts through USB-disconnected sleep and normal
button/motion wakes. Check timer alerts and midnight resets with each toggle.
Check slow and short drawer drags, reversal, a stationary held preview, and
committed/canceled pulls in both directions from every settings page. Interrupt
dragging and settling with the button and timer expiry, and verify that a held
finger cannot activate a newly visible tile or timer button. Physical smoothness
and LCD scan tearing need on-watch validation.
