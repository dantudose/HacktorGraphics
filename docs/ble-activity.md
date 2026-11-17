# BLE activity inspection

With Airplane mode off, the watch advertises as **HacktorWatch** while its
screen is awake. Connect with nRF Connect to read today's steps and estimated
distance. Screen timeout stops advertising and disconnects an attached phone;
BLE reads do not extend the backlight timeout. Wake the watch to reconnect.

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

1. Turn Airplane mode off. For more time to inspect, open Settings → More and
   set the screen timeout to 30 seconds. Wake the watch normally.
2. Scan in nRF Connect and connect to **HacktorWatch**. Discover services and
   expand the Watch Activity UUID shown above. A generic app may initially
   display its UUID instead of a friendly name.
3. Read the characteristics with the Read action; no notification subscription
   is needed. Read their `0x2901` descriptors if their labels are not shown.
4. Decode Steps as an unsigned 32-bit little-endian value. Decode Distance as
   an unsigned 64-bit little-endian value and divide by 1,000 for metres or
   1,000,000 for kilometres. If the app applies the presentation descriptor,
   it may already show metres; do not apply that conversion twice.
5. Walk, wait for the next normal visible step refresh (about three seconds),
   and read again. Let the screen time out to confirm the phone disconnects,
   then wake and reconnect.

For example, 1,000 steps produces `E8 03 00 00`. Its estimated distance is
747,000 mm = 747 m = 0.747 km, encoded as `F8 65 0B 00 00 00 00 00`.
The estimate uses the same 747 mm per step as the watchface. A step is one
footfall. These are today's totals, not a transfer of the stored two-week
history. The [daily step rules](daily-steps.md), midnight reset, clock
corrections, and flash restoration apply to both values.

Reads use the latest cached daily total. They do not initiate an IMU read or a
flash save. The main loop refreshes that cache through the existing visible
three-second sensor polling and wake refresh. If a sensor read fails, BLE
continues to report the last known total, just as the watch display does.
Reading the two values separately can straddle a step update or midnight; read
them again if a changing total needs to be compared precisely.

## Radio lifetime and battery

Advertising uses a one-second interval. Connection settings request a slower
interval for this read-only inspection; the phone determines the negotiated
parameters. Screen sleep and Airplane mode stop the peripheral session, so a
phone connection cannot keep it running in normal dark standby. Shutdown
finishes before processor light sleep. Ordinary timer alerts and the brief
shared-rail step-checkpoint glow do not start advertising.

The existing CTS time client shares the BLE stack. Scheduled time sync waits
while the activity peripheral owns it. Manual Sync time stops the peripheral
before attempting CTS; advertising can resume afterward if the screen is still
awake. A temporary disconnect during manual sync is therefore expected.

Advertising, connections, and BLE setup/teardown consume additional energy while
the screen is awake. There are no BLE-triggered background sensor polls,
notifications, flash writes, or periodic dark advertising. This limits the
added work but does not establish a battery-life figure; measure battery-side
current and a full discharge run on the watch.

## Automated validation

```sh
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_activity_data.cpp -o /tmp/hacktor-activity-data-tests
/tmp/hacktor-activity-data-tests
c++ -std=c++11 -Wall -Wextra -Werror -I test/ble_stubs -I include test/test_watch_activity_ble.cpp -o /tmp/hacktor-watch-activity-ble-tests
/tmp/hacktor-watch-activity-ble-tests
pio run -e hacktor_watch_s3_lgfx
```

Host checks cover wire formats, cached reads, connection/reconnection, radio
ownership, shutdown failures and retries, and millisecond rollover. They do not
emulate the controller or measure battery current.

## Hardware validation

- Compare both readable values with the watchface, including after a flash
  checkpoint/reset and after walking while asleep followed by a normal wake.
- Repeat connection, read, disconnect, and reconnect while the screen stays
  awake. Repeat several complete screen-sleep/wake cycles.
- Allow the screen timeout to expire during a connection. Confirm disconnection,
  disappearance from a fresh scan, and recovery on the next normal wake.
- Enable Airplane mode with a phone connected. Confirm disconnection and no
  advertisements, then disable it and reconnect while the screen is awake.
- Test manual time sync with a phone attached, midnight and clock changes, a
  running timer, and a timer alert. Verify the step cache remains consistent
  and no BLE operation extends the screen deadline.
- On battery with USB disconnected, measure dark standby after both connected
  and unconnected sessions. Verify radio shutdown does not leave the processor
  awake. Compare screen-on advertising and connection current with Airplane
  mode, accounting for the existing screen and motion-wake duty cycle.
