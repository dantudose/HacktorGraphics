# Countdown timer

A short switch press cycles **Analog → Digital → Timer → Analog**. An 800 ms
hold opens status from any of these screens. A short press on status returns to
the selected screen. Switching screens or entering status keeps the timer running.
While a face is lit, a left swipe selects the next face and a right swipe the
previous one, wrapping at either end. Swipes are disabled in status and standby.
Faces follow horizontal drags. Release at least 45 pixels from the start to
switch faces; a shorter drag slides back. Timer taps are canceled once a drag
locks, and controls ignore contacts begun during settling. Timer completion
and vibration servicing continue between frames.

## Touch controls

The default duration is **05:00**. Tap the minute or second digits to select the
field, then use **− / +**. Taps act when the finger lifts. Hold an adjustment
button to repeat: the first adjustment occurs after 500 ms, then repeats every
120 ms, with no extra adjustment on release. Moving more than 10 pixels from
the initial touch or leaving the control cancels the tap/hold. Swiping across
controls does not activate them; once a hold adjusts the timer, that contact
cannot also switch faces. Minutes clamp to 0–99 and seconds
to 0–59, without carrying into the other field. Start is disabled at 00:00.

**Start** begins the countdown; **Pause** freezes it and **Resume** continues it.
**Reset** cancels the countdown and restores its configured duration so it can
be edited again. On expiry, the timer automatically returns to that configured
duration, ready for another **Start**. It stays stopped; it does not repeat
automatically and needs no Done/acknowledgement step. The
configuration and running state reset to the five-minute default on reboot.

The display keeps its normal 10-second timeout and fade. Touching it while
setting or controlling the timer extends that timeout. Wake with the switch or
motion before using touch: the touch controller is unpowered in standby.

When the countdown finishes, the watch vibrates **six times**, with 150 ms
gaps, if Vibration is enabled in [Quick settings](settings.md). Muting vibration
still completes and resets the timer. It preserves the current face, status screen, display sleep state, and
backlight timeout. Timer shows its configured duration and setting controls as
soon as it expires, including when it is next selected. An old finger contact
cannot accidentally start the rearmed timer or activate controls after a screen
change.

## Timekeeping and standby

The countdown uses monotonic `esp_timer_get_time()` and is independent of the
wall clock and CTS corrections. Pause/resume retains fractional microseconds;
the display rounds positive remaining time up to whole seconds. There is no
per-second rendering or polling while dark. Processor light sleep is capped by
the exact remaining countdown, so completion is serviced without waiting for
the normal minute housekeeping wake. Completion wakes the processor for the
motor alert; it does not initialize or wake the display.

Scheduled blocking CTS scans wait while the timer is running or any watchface
is lit, and throughout the vibration/settling interval. They resume after
these activities finish. This keeps a scan from blocking a tap or delaying
completion/cleanup. Overdue CTS retries and battery polls do not prevent standby
during countdown or vibration. Battery reads remain deferred until a visible wake.

The timer reuses the shared canvas and digital face's flash digit coverage.
It adds no frame buffer or sprite. The displayed countdown redraws once its
whole-second value changes, or when a control, screen transition or wake makes
the screen dirty. The 4-bit low-memory renderer is supported too.

## Vibration and power

The DRV2605 at I²C `0x5A` plays two finite hardware batches of three single-click
effects, with five 150 ms gaps across the six clicks. A trailing wait in the
first batch separates the batches. Polling detects its termination and starts
the second batch once; the final batch stops without restarting. Each batch
stops the motor autonomously, including during a blocking display wake; a late
poll may lengthen the gap between batches but cannot extend a pulse. Configuration is
lazy, at completion or an explicit vibration-enable confirmation; it does not
calibrate or vibrate at boot. The ERM
open-loop profile caps full-scale average drive below 2.5 V. The schematic does
not identify the motor's exact rating, so strength/current need a watch check.
See the [TI DRV2605 datasheet](https://www.ti.com/lit/ds/symlink/drv2605.pdf)
for effect libraries, sequence waits/termination, and open-loop drive settings.

The settings enable-confirmation uses one finite ROM effect 15 (TI's 750 ms
alert), with a normal software stop scheduled at 500 ms. The finite waveform
limits playback even if the CPU is delayed. This soft stop does not shorten the
separate 3,000 ms fault watchdog: failed stop commands retain the motion guard
and retry at the usual 25 ms cadence. A 200 ms quiet interval follows a confirmed
stop. Timer completion can preempt confirmation and retains its pending six-pulse
request through settling; pending playback prevents a long standby sleep or
premature rail shutdown. No confirmation is generated by normal boot or expiry.

On a sleeping watch, only the shared peripheral supply and I²C are restored.
LCD and touch remain in reset; there is no panel initialization, drawing, touch
polling, or battery polling for this alert. The driver is polled every 25 ms
only during playback, with a 3,000 ms watchdog covering the whole six-pulse alert.
A 200 ms quiet interval follows
standby, then I²C is parked and the rail is switched off again. The processor can
light-sleep between alert deadlines. A normal button wake during playback still
works and keeps the restored rail available for the display.

The motor and backlight share `3V3_2`. With the reversed Q1, re-powering that rail
can briefly produce backlight glow even while the LCD stays asleep. Immediate
vibration with this brief glow is the chosen workaround until hardware repair.
See the [schematic](https://github.com/hacktorwatch/Hacktor-Watch-2.0/blob/main/Hardware/hacktor_watch2.pdf).

Motor-generated IMU wake events are acknowledged without waking the screen.
During playback/settling, IMU level wake is excluded so motor motion cannot keep
waking the processor; button and timer wake remain enabled. Normal motion wake
returns after settling. If clearing a latched motor interrupt fails, that stale
source stays suppressed until a successful clear or observed LOW; button wake
can recover I²C. Step counting and accelerometer configuration are retained.

The timer adds no continuous motor power or per-second standby polling. Actual
12-hour runtime still depends on measured current and alert frequency. Measure
one alert's battery charge and include it in the 250 mAh budget; for current
in mA and time in seconds, `charge_mAh = current × time / 3600`.

## Touch controller

The [Hacktor Watch 2.0 hardware](https://github.com/hacktorwatch/Hacktor-Watch-2.0#hardware-diagram--features)
uses a CST816T at I²C address `0x15`, reset GPIO2 and interrupt GPIO1. The driver
shares Wire on GPIO8/9; it polls every 20 ms only when Analog, Digital, or Timer
is lit and I²C is available. It does not enable a touch wake interrupt or poll
during a haptic-only wake. Touching any lit face extends the normal timeout.

At boot, reset is held LOW for 30 ms; display initialization supplies more than
the required 50 ms after reset is released. After a peripheral-rail power cycle,
the driver is initialized again. Auto-sleep is disabled while the controller is
powered; the existing standby workaround still removes its supply while dark.

The [CST816T register guide](https://admin.osptek.com/uploads/CST_816_T_v1_3_1_69c246954d.pdf)
defines the coordinate and touch-up fields. The driver handles explicit Up
events even when FingerNum is still one, rejects invalid coordinates/packets,
and does not invent releases from idle `0xFF` packets. I²C failures cancel the
contact and use a bounded retry backoff. Each transaction temporarily bounds
the Wire timeout, then restores it for the other bus users.

Touch coordinates default to the display's rotation 2. If physical alignment
requires a different mounting transform, set `HACKTOR_TOUCH_ROTATION` to 0–3 in
`platformio.ini` build flags. Check alignment on the actual watch before changing
it. Reset timing and auto-sleep handling also follow
[SensorLib's CST816 driver](https://github.com/lewisxhe/SensorLib/blob/master/src/touch/TouchDrvCST816.hpp);
the [Zephyr CST816 polling driver](https://github.com/zephyrproject-rtos/zephyr/blob/v4.1.0/drivers/input/input_cst816s.c)
provides a further protocol reference.

## Validation

```sh
pio run -e hacktor_watch_s3_lgfx
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_watch_timer.cpp -o /tmp/hacktor-watch-timer-tests
/tmp/hacktor-watch-timer-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_timer_touch.cpp -o /tmp/hacktor-timer-touch-tests
/tmp/hacktor-timer-touch-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_watch_swipe.cpp -o /tmp/hacktor-watch-swipe-tests
/tmp/hacktor-watch-swipe-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_watchface_touch.cpp -o /tmp/hacktor-watchface-touch-tests
/tmp/hacktor-watchface-touch-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_watch_touch.cpp -o /tmp/hacktor-watch-touch-tests
/tmp/hacktor-watch-touch-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_haptic_alert.cpp -o /tmp/hacktor-haptic-alert-tests
/tmp/hacktor-haptic-alert-tests
c++ -std=c++11 -Wall -Wextra -Werror -I test/haptics_stubs -I include test/test_watch_haptics.cpp -o /tmp/hacktor-watch-haptics-tests
/tmp/hacktor-watch-haptics-tests
```

On the watch, check touch alignment, tap and hold adjustment, zero/max durations,
pause/resume/reset, and long-press status from all three faces. Start a short
timer, select another face and let the display sleep: expect six distinct
vibrations, the accepted brief glow, and no normal screen wake or face change.
Repeat on every face and while status is open, both lit and asleep. Verify the
original fade deadline is retained when completion happens while lit. Check a
button wake during vibration, normal motion wake afterward, full rail off/on
cycles, and that a held contact cannot start the timer after automatic reset.
Return to Timer after expiry: the selected duration should be restored with
Start available, and no acknowledgement required. Leave it idle to verify there
are no repeated countdowns or alerts; deliberately start it again to repeat it.
Check a countdown across a scheduled CTS retry, then confirm sync resumes after
the timer stops and its controls are no longer visible. Confirm the panel still
goes fully dark after vibration/settling and measure standby and alert current.
