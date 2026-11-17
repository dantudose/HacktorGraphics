# Watchface rendering

## Watchfaces and controls

The watch starts with the analog face. A short button press cycles
Analog → Digital → Timer → Analog on release. Holding the button for 800 ms opens
status from any face; releasing that hold does not switch faces. A short press on status
returns to the selected face. A further long press on status also returns to
that face. The selected face survives screen sleep and resets to analog on boot.
The button wakes the panel as soon as its press passes the 30 ms debounce.

Swipe down from the upper part of any lit face to open [Quick settings](settings.md).
The panel follows the finger down to open and up to close, then settles or
returns to its original position on release. It owns touch while moving or open;
use the button to return immediately. A long
button hold opens Debug, now including screen-on, processor-sleep and wake statistics.

While a watchface is lit, swipe **left for the next face** or **right for the
previous face**. Both directions wrap around the three-face carousel. Faces
follow the finger once horizontal movement reaches 12 pixels and is at least
twice the vertical movement. A vertical gesture before that lock is rejected.
Once locked, the drag follows horizontal movement even with vertical drift;
reversing through the start point previews the other neighbour. There is no
time limit on a held drag.

Release at least 45 pixels from the start to select the previewed face; a
shorter drag slides back to the original. Completion and snap-back ease from
the current position over 60–200 ms, depending on the remaining distance.
The selected face changes only on a committed release. Button changes remain
immediate. Taps and vertical drags do not navigate. Status keeps its controls and
does not accept swipes. Touch never wakes a sleeping screen; use the button or
motion first. After wake or an external screen change, lift any existing finger
before starting another gesture.

The transition reuses the shared canvas: each frame composes the outgoing and
incoming faces in turn, transferring only their visible portions. Each transfer
finishes before the canvas is reused, and black pixels are copied too. There is
no additional frame buffer or display clear between faces. Both RGB565 with
antialiasing and the palette fallback are supported.

Animation advances through the main loop, targeting a 16 ms frame cadence.
Dragging redraws only when the position or preview side changes; a stationary
held preview requires no repeated composition. Settle frames skip ahead based
on elapsed time. Timer expiry, vibration servicing, and buttons continue between
frames. A button press interrupts dragging at the original face, or settling at
the selected face. Timer expiry or a touch transport error cancels an active
drag and restores the selected face. Touch contacts during settling are ignored
and must lift before activating controls. Display sleep cancels animation, and no animation work occurs in
standby. Frame rate and LCD scan tearing still need checking on the watch.

The digital face follows the reference layout: large stacked white hours and
mint minutes on the left; date, weekday, steps, battery, and estimated walking
distance on the right. It uses 24-hour time. The battery label changes to
`Charging` when the charger GPIO is active. Time digits contain precomputed
4-bit antialiasing coverage in flash; the normal RGB565 canvas blends their
edges. The palette fallback uses solid digit edges and the same layout.
`tools/generate_digital_digits.py` regenerates the digit asset from its geometry.

All screens share the existing 240 × 240 canvas. The additional faces allocate no
additional sprite or frame buffer. The digital face redraws on minute changes, new steps or
battery/charging data, time synchronization, wake, and screen transitions.
Rendering remains off in standby; peripheral power is briefly restored for
timer vibration without waking the LCD. Static screens yield
between polls instead of continually redrawing. Scheduled BLE scans wait while
a watchface is lit, a button gesture is active, or a screen transition is pending,
so they cannot interrupt a new swipe or timer tap. An already running CTS scan
still blocks the main loop as before.

The [Timer face](timer.md) uses touch to set minutes/seconds and provides
start/pause/resume/reset controls. It keeps counting across screen changes and
standby, vibrates six times at completion, and reuses the digital face's
antialiased digits. Completion preserves face/status, display sleep, and the
backlight deadline. The configured duration and setting controls return
automatically, ready for an explicit Start. Blocking CTS scans are also deferred
while a timer is running or a vibration alert is active.

Distance uses a hardcoded male profile, height **1.80 m**, and an estimated
step length of `1.80 × 0.415 = 0.747 m`:

```text
distance_km = step_count × 0.747 / 1000
```

The display labels this `km est.` and rounds to one decimal place. This is a
walking heuristic, not measured GPS distance. The [NXP pedometer reference](https://www.nxp.com/docs/en/reference-manual/MMA9553LSWRM.pdf)
uses the male height coefficient 0.415 within a larger algorithm; this app uses
the simple height-based estimate only. A measured walk can calibrate it later.
Distance follows the [daily step counter](daily-steps.md): both reset at midnight
on the watch's calendar and restore from the last flash checkpoint after reboot.
The firmware checkpoints changed totals every 30 minutes and retains today plus
14 previous dates. The 32-bit daily total accounts for the raw sensor's 16-bit
rollover.
The profile constants are near `active_watchface` in `src/main.cpp`.

## Analog hand antialiasing

The hour, minute and seconds hands use LovyanGFX's
`pushRotateZoomWithAA()` while the screen is visible. Their source sprites and
the cached dial keep their small 4-bit palettes. The compositing canvas uses
RGB565 so antialiased edges blend into the dial and other hands with the correct
colors. Text and the shoe icon stay at their native pixel size.

The RGB565 canvas needs 115,200 pixel bytes, an increase of 86,400 bytes
(approximately 84 KiB) over the original 240 × 240, 4-bit canvas. If its allocation
fails, the app falls back to the original palette renderer without hand
antialiasing and prints a message on the serial console. Essential dial and hand
buffers are allocated first. If even the smaller canvas cannot be allocated,
the app reports the failure and leaves USB responsive rather than drawing into
an invalid buffer.

The completed frame is transferred opaquely. Both transparent index 0 and black
index 6 from the cached sprites become the same RGB565 black; treating black as
transparent on the final transfer would leave old hands or status text visible.
The status screen also uses this canvas and still composes a complete frame
before sending it to the panel.

To select the lower-memory renderer explicitly, add this to the environment's
`build_flags` in `platformio.ini`:

```ini
  -D HACKTOR_HAND_ANTIALIASING=0
```

The default is `1`. The app still stops drawing when dark; the peripheral-rail
workaround and light-sleep behavior operate independently of this setting.

After flashing, inspect the hands at shallow angles, the red seconds hand over
white hands and dial text, and transitions between all three faces and status.
Test short presses and long holds from all faces, including while waking from
sleep. A long hold must open status once and must not toggle the face on release.
Swipe both directions through every face, including wrapping at each end. Swipe
across timer controls without activating them, and check that a held adjustment
still repeats but cannot subsequently turn into a face change. Confirm touches
while dark do not wake or change the face, including during a timer alert.
Check slides in both directions between every pair of faces, short/long button
presses interrupting a slide, and timer completion during a slide. No frame
should briefly show status, clear to black, or accept a tap begun during motion.
Check slow drags, stationary holds, reversing through zero, release below the
45-pixel threshold, and dragging across timer controls. A drag should track the
finger and settle continuously from its last position, including snap-back.
Check digital minute changes, steps/distance, charging, and date rollover.
Check repeated power-off/wake cycles and a BLE time-sync attempt too. Visual
smoothness, animation speed, available heap and battery impact need confirmation
on the watch; a firmware build alone does not measure them.

The native transition tests cover live tracking, reversal, snap-back, stationary
holds, timer ownership, easing, adjacent face placement, cadence, dropped frames,
and millis rollover:

```sh
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_watchface_slide.cpp -o /tmp/hacktor-watchface-slide-tests
/tmp/hacktor-watchface-slide-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_watchface_drag.cpp -o /tmp/hacktor-watchface-drag-tests
/tmp/hacktor-watchface-drag-tests
```

Reference: [LovyanGFX sprite rotation APIs](https://github.com/lovyan03/LovyanGFX/blob/master/src/lgfx/v1/LGFX_Sprite.hpp).
