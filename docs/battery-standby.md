# Battery standby

The app defaults to full brightness, a 10-second screen
timeout, a 1-second fade, an animated analog watch face, motion wake,
104 Hz accelerometer configuration, and pedometer. Motion detection is still the
existing acceleration-triggered wake; this change does not introduce wrist-tilt
classification or prevent walking from repeatedly extending the timeout.
The digital and timer faces share the existing canvas and redraw when data
changes. A short button press cycles Analog → Digital → Timer → Analog; an 800 ms
hold opens status from any face, and a short press on status returns to the
selected face. A running [countdown timer](timer.md) continues in standby and
adds a deadline wake for six vibration pulses. Completion preserves the face,
status screen, sleep state, and backlight timeout. The timer then returns to its
configured duration and stays stopped until another explicit Start.
Left/right touch swipes also cycle faces while a watchface is lit. Touch is
polled every 20 ms during that time only; it adds no standby polling, wake source,
or frame buffer. Touching a lit face extends its normal timeout.
Faces follow horizontal finger drags on the same canvas, then settle or slide
back on release. Stationary held previews do not repeatedly redraw. Extra
rendering is confined to that visible interaction; timer/haptic tasks continue between frames, and
display sleep cancels the animation. Battery impact needs measurement on hardware.

[Quick settings](settings.md) provide brightness (0–100 mapped to 10–100% PWM),
5/10/15/30-second timeout, airplane mode, vibration and motion wake preferences.
They save only committed changes and add no standby polling. Airplane mode
suppresses BLE work and its sleep deadlines; motion wake off preserves steps
and button wake. Vibration off mutes alerts without stopping the countdown.
The long-press debug screen shows screen-on time, successful light sleep and
screen wake counts to help measure normal use.
[BLE activity inspection](ble-activity.md) advertises as `HacktorWatch` while
the screen is awake, exposing readable daily steps and estimated distance.
Screen timeout stops advertising and disconnects the phone before processor
light sleep. BLE reads do not extend the screen deadline. The one-second
advertising interval and read-only values limit extra work, but advertising,
connections, and setup/teardown still add screen-on energy use; measure their
effect on battery life.
The settings panel also follows vertical finger drags using the same canvas.
Stationary previews do not redraw, and opening/closing settle through the main
loop so scheduled services continue. Sleep cancels the panel motion normally.

[Serial logging](logging.md) defaults to off through `HACKTOR_LOG_LEVEL=0` in
`platformio.ini`. Disabled messages skip formatting and serial writes; diagnostic
I²C scans, register dumps, and periodic traces only run at their enabled levels.
USB initialization and host detection remain available for firmware uploads.

[Daily steps](daily-steps.md) add sensor snapshots at midnight and every 30
minutes, including in standby. The LCD stays asleep; brief backlight glow can
occur while the shared rail is restored. Midnight closes the
outgoing date's total and starts the new date. Periodic checkpoints save changed
dated totals to ESP32 NVS so steps and estimated distance survive resets, with
today and the previous 14 dates retained. Unchanged totals skip flash writes;
step history attempts at most 48 saves per day, starting 30 minutes after boot.
There is no per-second standby step polling. Abrupt reset normally loses at
most the last 30 minutes, with failed reads/writes or service delays extending
that window.

## Backlight rail workaround

Q1 is reversed in the [Hacktorwatch schematic](https://github.com/hacktorwatch/Hacktor-Watch-2.0/blob/main/Hardware/hacktor_watch2.pdf),
so setting backlight PWM to zero alone does not fully extinguish the light.
The default software workaround turns off the shared `3V3_2` supply through
GPIO18 when the fade ends. The main `3V3` supply stays on for the ESP32-S3 and IMU;
the fuel gauge keeps running from `VBAT`.

Before switching the rail off, the app finishes display transfers, releases SPI,
and drives display GPIO3 (reset), GPIO4 (CS), GPIO5 (DC), GPIO11 (MOSI), GPIO12
(clock), and touch-reset GPIO2 LOW. It stops I2C and parks SCL GPIO9 LOW before
parking SDA GPIO8 LOW. This prevents the main-supply I2C pull-ups from feeding
the unpowered touch and haptic devices through their shared bus connections.
Holding both 3.3 kΩ pull-ups LOW adds approximately 2 mA on the main 3.3 V rail;
this is an estimate, not a measured battery current.

The switched rail also supplies touch, haptics, audio, and external NOR flash.
Those devices are normally off while dark; the app briefly restores the shared
rail for a timer vibration, midnight step baseline, or 30-minute step
checkpoint. With reversed Q1 this can cause brief glow, even when
an unchanged count means the checkpoint skips writing flash. The history itself
uses ESP32 main flash, independently of the external NOR chip. The LCD/touch
stay in reset. The rail is parked after a sensor snapshot, or after playback and
200 ms of settling. The app uses touch while
any watchface is lit. The IMU continues counting steps and can signal motion while
I2C is parked. On button
or motion wake, the app restores GPIO18, waits 5 ms for the rail, restarts I2C at
400 kHz, and fully initializes and rotates the display. It refreshes battery and
step readings before drawing the face and enabling the backlight.
The touch controller is initialized again after each rail power cycle.

The workaround is enabled by `HACKTOR_BACKLIGHT_RAIL_WORKAROUND`, which defaults
to `1` in `src/main.cpp`. To restore panel-only sleep, add this build flag:

```ini
  -D HACKTOR_BACKLIGHT_RAIL_WORKAROUND=0
```

With that flag set to `0`, GPIO18 stays enabled and the panel uses sleep/wake
commands with 120 ms transition delays. This mode does not fix glow caused by
Q1's orientation.

## Processor standby and timekeeping

While dark, the app stops rendering and enters ESP32-S3 light sleep with button
GPIO0 (LOW), IMU GPIO17 and GPIO14 (HIGH), and a timer wake for scheduled work.
The timer wakes at least once every 60 seconds to check for a USB host. USB host
connection skips processor sleep so the serial console and firmware upload
remain available. These USB housekeeping checks do not restore the switched
rail or light the screen; step snapshots use their separate deadlines.

Battery reads occur once per minute while visible and on every user wake. Dark
standby does not read I2C or briefly power the rail for a battery check. The
midnight step baseline and 30-minute checkpoints are separate scheduled sensor
reads. BLE CTS
synchronization remains hourly after a successful sync, with at least five
minutes between failed attempts. BLE is stopped and its objects released after
each attempt; synchronization can run with `3V3_2` off and I2C parked.
The activity peripheral and CTS client share the BLE stack. Scheduled CTS waits
until peripheral shutdown completes; manual time sync stops the peripheral
first. Step inspection reads the latest cached visible total without I²C,
flash writes, notifications, or additional dark sensor polling. Timer-only and
step-checkpoint wakes do not start activity advertising.
Scheduled scans are deferred while a button gesture or screen transition is
pending, so they do not delay that action before its first frame.
Scans also wait while a countdown is running, any watchface is lit, or a
vibration alert is active, to
keep timer completion and touchscreen controls responsive. Standby can still
sleep when a CTS retry is overdue but deferred for a running countdown.
The five minutes before midnight also defer blocking sync, with sleep capped
by the daily step deadline rather than an overdue scan.

The motor plays two finite three-click hardware batches; the processor may sleep
between its 25 ms status polls and launches the second batch once. There is a
3,000 ms watchdog for the whole alert, followed by 200 ms of
settling and rail cleanup. Motion wake is temporarily excluded during this
bounded interval so motor motion cannot illuminate the screen or continually
wake the processor; button wake stays available. No per-second countdown work
or haptic-driver initialization occurs in ordinary standby.

The clock uses a 64-bit `esp_timer_get_time()` anchor:
ESP-IDF advances that timer by the RTC-measured elapsed time after light sleep,
so pauses and scans no longer lose whole seconds. RTC oscillator drift is still
possible and needs measurement on the watch. After reset, a valid step-history
checkpoint supplies its saved date/time as the clock fallback; first use or
missing/corrupt history uses the compile date/time. There is no persistent RTC
to recover elapsed time across reset or power loss. CTS/manual correction is
still needed, and a stationary watch's saved time can be old because unchanged
step totals do not trigger a timestamp-only flash write.

The IMU wake interrupt is explicitly latched (`TAP_CFG = 0x81`) until its source
register is read. The sleep code disables normal IMU edge interrupts before
arming level wake, checks pending events, then restores edge interrupts after
wake. SPI transactions finish after each frame.

## Build and automated checks

```sh
pio run -e hacktor_watch_s3_lgfx
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_watch_timing.cpp -o /tmp/hacktor-watch-timing-tests
/tmp/hacktor-watch-timing-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_watch_button.cpp -o /tmp/hacktor-watch-button-tests
/tmp/hacktor-watch-button-tests
```

The host tests cover elapsed time across scans and long sleeps, fractional
milliseconds, forward/backward CTS corrections, midnight, retries, and 32-bit
millisecond rollover. They do not emulate the panel, BLE controller, or GPIOs.
The button tests cover 12 scenarios including bounce, short/long separation,
blocking display initialization, held-at-boot suppression, and timer rollover.

## Watch validation

1. With USB connected, verify all three faces, brightness, fade, short-press face
   switching and long-press status,
   serial monitor, and firmware upload. Check that the screen is completely dark
   after the fade, including in a dark room. The rail still switches off on USB,
   but processor sleep is skipped. Use battery power for standby-current tests.
2. On battery, let the panel turn off while the watch is stationary. Wake it with
   motion and with the button separately, and repeat. Check the time, steps, info
   screen, and fade. Also wake it immediately as the fade ends and hold/release
   the button through the timeout.
3. Leave it stationary and dark for several minutes. The screen must not flash
   during minute housekeeping or five-minute CTS retries without an advertiser.
   Confirm that the first user wake shows elapsed time and refreshed battery and
   steps, and that the reset count has not increased.
4. Walk while it is dark and compare the steps before/after wake. Check repeated
   motion during the visible interval and around a scheduled time-sync attempt.
5. With a CTS advertiser available, verify a successful boot sync and a subsequent
   hourly sync after standby. Repeat with the advertiser removed and restored to
   exercise BLE shutdown/reinitialization on failed and successful attempts.
6. Check USB plug/unplug and extended standby across midnight. Allow up to one
   minute for USB detection during standby. Compare the watch against a reference
   clock to measure sleep-clock drift.
7. Expire a timer on each face and in status, both lit and asleep. Verify exactly
   six vibrations, automatic return to the configured stopped timer, preserved
   face/status and timeout, no normal display wake,
   brief glow only for the accepted shared-rail alert, and restored dark standby
   afterward. Check button wake during vibration and motion wake after settling.
8. With wrist wake off, walk through a 30-minute checkpoint in dark standby,
   then reset and check that the saved steps/distance return and keep increasing
   normally. A checkpoint may briefly glow but must preserve the face, normal
   screen-wake count, and timeout. Repeat stationary checkpoints and verify
   unchanged dated totals skip flash writes. Test midnight followed by a
   checkpoint/reset; use [daily-step validation](daily-steps.md#validation) for
   retention and failure cases.

Measure battery-side current in stationary standby, with the face lit, during
BLE synchronization, timer vibration, step checkpoints, and during normal wear.
Include expected alert frequency and two step snapshots per hour in the average.
For the 250 mAh battery, 12 hours
allows an ideal average of 20.8 mA; aim for 16–17 mA to leave usable-capacity
margin. Confirm the result with a full discharge run. Rail cycling, removal of
the glow, wake recovery, standby current, and 12-hour battery life still require
watch measurements.

References: [ESP-IDF light sleep](https://docs.espressif.com/projects/esp-idf/en/v5.1.4/esp32s3/api-reference/system/sleep_modes.html),
[ESP timer during light sleep](https://docs.espressif.com/projects/esp-idf/en/v5.1.4/esp32s3/api-reference/system/esp_timer.html#esp-timer-during-light-sleep),
[LSM6DSL datasheet](https://www.st.com/resource/en/datasheet/lsm6dsl.pdf),
[TI DRV2605 datasheet](https://www.ti.com/lit/ds/symlink/drv2605.pdf),
[Richtek RT6160A datasheet](https://www.richtek.com/assets/product_file/RT6160A/DS6160A-05.pdf).
