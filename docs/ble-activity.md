# BLE activity inspection

Open **Settings → More → BLE advertising** to enable or disable the activity
peripheral. The setting defaults to **On** and survives a reset. With it enabled
and Airplane mode off, the watch wakes for a **three-second advertising window
every five seconds** as **HacktorWatch**, then shuts BLE down so the processor can
return to light sleep. If a phone connects, it gets a bounded inspection session
of up to **30 seconds** from the first observed connection in that cycle before
disconnecting. The screen does not control this schedule. BLE reads do not
illuminate the display or extend its timeout. Turning the toggle Off stops the
peripheral and disconnects the phone; turning it On starts a new window promptly.

The Watch Activity service exposes two read-only custom characteristics:

| Attribute | UUID | Value |
| --- | --- | --- |
| Watch Activity service | `b44e0001-2cf5-4b4a-9f4d-8a6f4e7d2c10` | Primary service |
| Daily steps | `b44e0002-2cf5-4b4a-9f4d-8a6f4e7d2c10` | Unsigned 32-bit integer, little-endian, footfalls |
| Estimated distance | `b44e0003-2cf5-4b4a-9f4d-8a6f4e7d2c10` | Unsigned 64-bit integer, little-endian, millimetres |

Each characteristic includes a readable User Description descriptor (`0x2901`)
with its label and a Characteristic Presentation Format descriptor (`0x2904`).
The steps format is `uint32`, exponent 0, unitless. The distance format is
`uint64`, exponent −3, metres: multiply the stored integer by 0.001 to obtain
metres. The seven descriptor bytes are respectively
`08 00 00 27 01 00 00` and `0A FD 01 27 01 00 00`. This uses the standard
[GATT presentation format](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-61/out/en/host/generic-attribute-profile--gatt-.html)
for custom characteristic values.

Bluetooth's standard Physical Activity Monitor Service includes a Step Counter
Activity Summary Data characteristic, but it carries session metadata and
optional measurements through indications, alongside other required service
characteristics. It is not a simple readable daily counter. This initial
inspection service uses its own UUIDs and does not claim to implement that
profile. See the
[Physical Activity Monitor Service specification](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/PAMS_v1.0/out/en/index-en.html).

## Read with nRF Connect

1. Turn Airplane mode off and enable Settings → More → BLE advertising. It is
   On by default; the screen can time out while you inspect the values.
2. Scan in nRF Connect and connect to **HacktorWatch**. Discover services and
   expand the Watch Activity UUID shown above. A generic app may initially
   display its UUID instead of a friendly name. Leave a foreground scan running
   for a few cycles (about 10–15 seconds): a short window can be missed, and
   discovery is not guaranteed in the first cycle. A cached scan result can remain visible after
   the window closes; connecting may require the next window. Turning the
   setting Off and On starts a new window promptly.
3. Read the characteristics with the Read action; no notification subscription
   is needed. Read their `0x2901` descriptors if their labels are not shown.
4. Decode Steps as an unsigned 32-bit little-endian value. Decode Distance as
   an unsigned 64-bit little-endian value and divide by 1,000 for metres or
   1,000,000 for kilometres. If the app applies the presentation descriptor,
   it may already show metres; do not apply that conversion twice.
5. Read during the 30-second connection session. Walking with the screen awake
   refreshes steps normally about every three seconds. The phone disconnects
   when the session expires; reconnect during a later advertising window. For a
   fresh value after walking with the screen asleep, wake it normally or wait
   for the next 30-minute step checkpoint.

For example, 1,000 steps produces `E8 03 00 00`. Its estimated distance is
747,000 mm = 747 m = 0.747 km, encoded as `F8 65 0B 00 00 00 00 00`.
The estimate uses the same 747 mm per step as the watchface. A step is one
footfall. These are today's totals, not a transfer of the stored two-week
history. The [daily step rules](daily-steps.md), midnight reset, clock
corrections, and flash restoration apply to both values.

Reads use the latest cached daily total. They do not initiate an IMU read or a
flash save. The main loop refreshes that cache through the existing visible
three-second sensor polling, wake refresh, midnight snapshot, and 30-minute
checkpoint. With the screen asleep, a read can therefore return an older total;
it does not force another shared-rail wake. If a sensor read fails, BLE
continues to report the last known total, just as the watch display does.
Reading the two values separately can straddle a step update or midnight; read
them again if a changing total needs to be compared precisely.

## Radio lifetime and battery

Within each three-second discovery window, advertising uses a 250 ms
interval. Connection settings request a slower interval for this read-only
inspection; the phone determines the negotiated parameters. An attached phone
can keep the cycle active only for the bounded 30-second connection session;
reads and reconnects do not renew it indefinitely. Between windows, the host
and controller shut down before normal processor light sleep. Window deadlines
use monotonic elapsed time rather than CTS wall-clock time. A failed startup
also consumes its discovery attempt until the next five-second cycle rather
than spinning on initialization retries. These are scheduler deadlines, not hard radio-on
limits: normal shutdown and an explicit manual CTS scan/connection can extend a
window until the main loop resumes. Automatic CTS waits between windows, so it
does not stretch an active discovery burst. Blocking work can delay the start
of a later window. After manual CTS cleanup, the main loop applies the elapsed
advertising/session deadline.

A connected session can outlast the five-second interval. The next window waits
until the previous radio session has fully shut down; missed windows do not
trigger a burst of catch-up attempts.

The saved advertising setting controls this schedule independently of the
screen. Airplane mode is a master radio switch: it suppresses both the
peripheral and CTS but preserves the advertising preference.

The advertising control does **not** disable CTS time synchronization. The two
features share one BLE stack, and this SDK supports only one connection at a
time. Automatic CTS runs only between activity windows. Manual CTS also waits
while a phone is connected or the peripheral is shutting down; disconnect the
phone to allow a sync. With no phone attached, a manual attempt can reuse an
active activity stack without deleting its server; cleanup releases only the
CTS client. If a phone joins during the scan, CTS checks the slot again before
starting its outgoing connection. Turning advertising off still allows normal
startup, scheduled, retry, and manual CTS attempts.

The current precompiled Arduino SDK disables power management and Bluetooth
modem sleep (`CONFIG_PM_ENABLE` and `CONFIG_BT_CTRL_MODEM_SLEEP` are unset).
The processor therefore stays out of manual light sleep while BLE is running.
The short discovery windows deliberately shut down the radio between uses;
without a phone, they add about three seconds of awake time per five-second
cycle plus initialization/shutdown overhead, leaving roughly two seconds for
light sleep minus that overhead. This increases battery load substantially
compared with one window per minute. A connected session adds more awake time,
bounded by its deadline. Automatic light sleep with BLE running requires
compatible SDK power-management support; it cannot be enabled by a runtime call
in this build. See
[Espressif's power-management requirements](https://docs.espressif.com/projects/esp-idf/en/v5.1.4/esp32s3/api-reference/system/power_management.html)
and [Bluetooth and sleep modes](https://docs.espressif.com/projects/esp-idf/en/v5.1.4/esp32s3/api-reference/system/sleep_modes.html#wi-fi-bluetooth-and-sleep-modes).

Turn BLE advertising off to remove these discovery wakes entirely; normal
processor light sleep then continues between CTS and other scheduled work.
There are no BLE-triggered background sensor polls,
notifications, or flash writes. Changing the toggle saves only the changed
preference. Measure battery-side current with advertising on and off, connected
and disconnected, and confirm battery life with a full discharge run; no runtime
battery figure has been established.

## Automated validation

```sh
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_activity_data.cpp -o /tmp/hacktor-activity-data-tests
/tmp/hacktor-activity-data-tests
c++ -std=c++11 -Wall -Wextra -Werror -I test/ble_stubs -I include test/test_watch_activity_ble.cpp -o /tmp/hacktor-watch-activity-ble-tests
/tmp/hacktor-watch-activity-ble-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_ble_advertising_schedule.cpp -o /tmp/hacktor-ble-advertising-schedule-tests
/tmp/hacktor-ble-advertising-schedule-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_cts_ble_cleanup.cpp -o /tmp/hacktor-cts-ble-cleanup-tests
/tmp/hacktor-cts-ble-cleanup-tests
pio run -e hacktor_watch_s3_lgfx
```

Host checks cover wire formats, cached reads, connection/reconnection, radio
ownership, shutdown failures and retries, window/session deadlines, and
millisecond rollover. They do not
emulate the controller or measure battery current.

## Hardware validation

- Compare both readable values with the watchface, including after a flash
  checkpoint/reset and after walking while asleep followed by a normal wake.
- Scan across several cycles with the screen both awake and asleep. Verify one
  three-second discovery window every five seconds and light sleep between
  windows.
  Connect during a window, read both values, and verify automatic disconnection
  after the bounded session. Reads and reconnects must not extend it indefinitely.
- Disable BLE advertising with a phone connected. Confirm disconnection and
  disappearance from a fresh scan. Reset and verify that Off is retained, then
  turn it On and verify a new discovery window, reset, and verify that On is
  retained. An upgrade without a saved preference should default to On.
- Enable Airplane mode with a phone connected. Confirm disconnection and no
  advertisements, then disable it and verify that the advertising preference
  resumes without changing it.
- With BLE advertising Off and Airplane mode off, verify startup/manual CTS and
  a subsequent scheduled attempt. With advertising On and no phone attached,
  verify automatic CTS runs between windows. Manually sync during a window and
  verify the activity service survives CTS cleanup, then applies its elapsed
  deadline. While connected from nRF Connect, verify CTS waits; disconnect and
  retry it. Check a phone joining during manual scan does not get displaced by
  an outgoing CTS connection.
- Test midnight and clock changes, a running timer, and a timer alert. Verify
  the cached total stays consistent and a read does not light the screen.
- On battery with USB disconnected, compare dark current with discovery On and
  Off, both connected and disconnected. Verify light sleep returns after each
  advertising window or connection session. Account for discovery frequency,
  connection use, existing screen and motion-wake duty cycle in a full discharge
  run.
