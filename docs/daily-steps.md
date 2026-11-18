# Daily steps

Steps and estimated distance restart at **00:00 on the watch's displayed
calendar date**, on every face and the status screen. The IMU keeps its running
hardware counter. A successful midnight snapshot adds the last unsampled steps
to the outgoing date, including steps taken while the display was asleep, then
becomes the new date's baseline. The daily total uses 32 bits and handles the
hardware counter's 16-bit rollover when fewer than 65,536 steps pass between
samples. Warm-reset recovery also requires fewer than 65,536 steps since the
persisted raw-counter anchor; prolonged save failures can exceed that limit.

## History and flash checkpoints

`StepTracking` keeps dated daily totals in RAM through `StepHistory`. Its 15
slots hold today and the previous 14 calendar dates, including across month,
year, and leap-day boundaries. Missing dates stay absent; the firmware does not
invent measurements while powered off. Each record contains an absolute
Gregorian day number and a 32-bit step count. Estimated distance is recalculated
from those steps using the existing fixed stride estimate, so the displayed
distance survives a reset with its step total.

[BLE activity inspection](ble-activity.md) exposes today's cached steps and
estimated distance as read-only characteristics for nRF Connect when its saved
advertising toggle is On. Three-second discovery windows run every five seconds,
independently of the screen, with phone sessions bounded to 30 seconds. It does
not transfer historical records, add sensor polling, or force a flash checkpoint.
Dark reads can retain the last known total until the next 30-minute checkpoint
or a normal visible wake; reading BLE itself does not refresh the sensor.

`StepCheckpoint` schedules a sensor snapshot every **30 minutes**, both awake
and asleep. After sampling, firmware writes only if the dated totals or history
or their raw-counter reference have changed since the last successful save.
Unchanged values do not write just to refresh a timestamp. The normal deadline
starts 30 minutes after boot; save attempts and unchanged periodic checks start
a new 30-minute interval. Delayed service does not trigger catch-up writes.

A new sensor/day baseline needs a durable raw reference before it can support
warm-reset recovery. Cold setup, legacy migration, midnight, and other date
changes therefore save that anchor once, without waiting for the normal
interval. The initial anchor is saved before the potentially blocking boot CTS
attempt; a CTS date correction then saves its own dated anchor if needed.
Pending anchor failures retain their RAM state and retry at
most once per minute; ordinary periodic save failures wait for the next
30-minute checkpoint. There are at most 48 ordinary periodic attempts per day,
plus these baseline-anchor saves and their bounded retries. A verified warm
reset on the same date does not force another step-history write.

The entire snapshot is one **148-byte version-2**, little-endian blob with
CRC-32, stored under the retained `steps-v1` key in the existing `watch` NVS
namespace. It contains the dated 32-bit totals, checkpoint clock time, and a
16-bit raw-counter anchor with a validity flag. This uses
the ESP32's main flash, independently of the external NOR chip on the switched
peripheral rail. Loading never writes. Missing, truncated, unsupported, or
invalid snapshots are rejected without partially restoring their contents.
Legacy 144-byte version-1 snapshots still load, but have no raw reference; the
first upgraded boot establishes and saves one. Unsaved activity from before
that upgrade cannot be reconstructed from a version-1 record.

NVS appends replacement values to its log and reclaims pages, distributing
flash use; a checkpoint does not imply a sector erase. Espressif documents
recovery after power loss, with the value being written potentially lost. The
CRC checks the application snapshot as well. These mechanisms do not guarantee
a particular flash lifetime or preserve unsaved RAM data. See
[Espressif's NVS storage and robustness documentation](https://docs.espressif.com/projects/esp-idf/en/v5.1.4/esp32s3/api-reference/storage/nvs_flash.html).

## Recovery after reset

On a CPU/USB reset, the IMU can remain powered and keep counting. Startup probes
it **before loading step history from flash**: it restores the user register
bank, checks identity and 13 expected configuration registers, and takes a
coherent raw-counter snapshot. If the running configuration and committed-anchor
marker are valid, startup keeps the sensor running instead of issuing
`SW_RESET` or reconfiguring the pedometer. It then restores the saved daily
total and adds `uint16_t(live_raw - saved_raw)`, including unsampled activity
since the last save. This also works before the first normal 30-minute
checkpoint, once the initial raw anchor has been committed.

The live 16-bit value is a free-running counter, not a dated daily total. Its
saved association with the 32-bit total prevents double-counting and handles
one counter rollover. ST documents the counter and its retention through
accelerometer power-down/pedometer disable in
[AN5040, sections 5.7 and 6.1](https://www.st.com/resource/en/application_note/dm00402563.pdf).
Recovery does not rely on an assumption that an IMU software reset preserves
that counter: a verified warm startup avoids the reset entirely.

After sensor power loss or an unverified configuration/read, firmware follows
the existing cold reset/setup path, restores only the saved daily total, takes
a fresh raw baseline, and commits a new anchor once. That fallback can lose
the last **30 minutes** of unsaved activity; sensor/save failures or delayed
service can extend the loss window. A missing or invalid history has no safe
daily reference for the live raw counter and starts a new record.

To prevent a reset during setup from combining an old flash reference with a
new sensor/day baseline, firmware leaves `CTRL3_C=0x04` until that anchor is
durable. Only a successful save permits `0x44`, which the warm probe requires.
The changed bit is BDU, which controls unused accelerometer/gyro output-byte
latching; pedometer counting stays enabled and the step reader checks byte
coherence in software. Date-baseline changes clear and verify this marker
before changing their reference. This scheme relies on the current no-FIFO
configuration: FIFO use would require BDU enabled. See
[AN5040, section 4.4 and the FIFO requirements](https://www.st.com/resource/en/application_note/dm00402563.pdf).

The persisted checkpoint date/time also supplies the boot clock fallback when
valid, preventing the firmware build date from replacing the saved activity
date. Without a valid snapshot, the compile-time fallback is used. There is no
persistent RTC to measure elapsed time across reset or power loss; CTS or manual
time setting must correct that fallback. A stationary watch can retain an old
checkpoint time because unchanged totals do not justify a flash write. Live
counter recovery alone cannot identify which side of an unobserved midnight
steps belong to; exact assignment while the processor was unavailable remains
limited by that clock fallback.

Same-date clock corrections preserve the total. Selecting another retained
date restores its count and establishes a fresh sensor baseline; a date with no
record starts at zero. Backward corrections within the retained window preserve
newer records. Correcting more than 14 days behind the newest record starts a
new window, clearing obsolete future history so an erroneous clock cannot
prevent saving the corrected date.

## Sensor service and standby

At startup, keep the CST816T touch controller in reset while initializing the
IMU on their shared I²C bus. Releasing touch first caused zero IMU register
readbacks on the watch, despite successful acknowledgements. Hold touch reset
LOW for at least 30 ms and throughout IMU setup, then release it and wait 50 ms
before using touch. This runs once at boot and adds no standby polling.
The long-press debug screen shows the raw sensor count or an initialization,
configuration, or read error to distinguish a sensor failure from zero steps.

The existing clock includes processor light sleep. The next midnight and
30-minute checkpoint are added to the sleep deadline, alongside timer/vibration
and existing housekeeping deadlines. For either sensor snapshot, firmware
briefly restores peripheral power and I²C, captures the raw counter, then parks
the rail again. It does not initialize or wake the LCD, change the face, extend
the backlight deadline, poll touch, or refresh the battery. An active motor
alert shares the restored rail.

With Q1 reversed, this brief rail restore can cause backlight glow during
midnight snapshots, periodic 30-minute checkpoints, and sleeping timer alerts.
A checkpoint can briefly glow even if the counter is unchanged and no flash
write follows. The service runs on the
first scheduler pass at the boundary; normal rendering or peripheral operations
can add latency.

There is no per-second standby step polling. Visible step refresh remains every
three seconds and on wake, updating RAM without writing flash. If a midnight
read fails, firmware preserves the outgoing date's known total and displays the
new date's restored count, normally zero. Its pending baseline retries at most
once per minute; a normal visible read can also recover it. Steps before a
successful baseline cannot be assigned to the new day during such a failure.

Reads temporarily use a 10 ms Wire timeout, restored afterward, and at most two
coherence attempts. A high-byte sample followed by a low/high burst rejects
counter rollover during the read. Failed or torn reads leave the previous
baseline untouched.

Scheduled blocking CTS work is deferred during the five minutes before midnight,
since BLE connection retries can otherwise span the reset. It resumes afterward;
overdue sync does not cause a busy loop during that window.

## Validation

```sh
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_daily_steps.cpp -o /tmp/hacktor-daily-steps-tests
/tmp/hacktor-daily-steps-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_step_history.cpp -o /tmp/hacktor-step-history-tests
/tmp/hacktor-step-history-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_step_checkpoint.cpp -o /tmp/hacktor-step-checkpoint-tests
/tmp/hacktor-step-checkpoint-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_step_tracking.cpp -o /tmp/hacktor-step-tracking-tests
/tmp/hacktor-step-tracking-tests
c++ -std=c++11 -Wall -Wextra -Werror -I include test/test_lsm6dsl_startup.cpp -o /tmp/hacktor-lsm6dsl-startup-tests
/tmp/hacktor-lsm6dsl-startup-tests
c++ -std=c++11 -Wall -Wextra -Werror -I test/step_stubs -I include test/test_step_counter_read.cpp -o /tmp/hacktor-step-counter-read-tests
/tmp/hacktor-step-counter-read-tests
pio run -e hacktor_watch_s3_lgfx
```

Host tests cover dated retention, warm recovery from a saved raw anchor,
cold/legacy restoration, rollover/wrap, malformed and corrupted blobs, failed
writes, unchanged save suppression, startup configuration validation,
30-minute scheduling and millisecond rollover. They do not measure hardware
power or emulate NVS power interruption.

On the watch:

1. After the initial anchor save, walk and reset the CPU before 30 minutes.
   Verify unsaved steps/distance return on Analog, Digital, status, and BLE,
   then keep increasing normally. Repeat several warm resets on the same
   date; they must not cause extra step-history saves. Repeat after a normal
   checkpoint and near the raw counter's rollover.
2. Repeat on battery with wrist wake off, walking while the screen is asleep
   through a checkpoint. Check for potential brief rail glow, preserved face
   and sleep state, and restored asleep steps after reset. Leave it stationary
   across later checkpoints and check logs/instrumentation for no history
   write when counts are unchanged. Measure the added checkpoint energy.
3. Set the clock shortly before midnight using CTS or Settings. Verify zero
   steps/distance at rollover, outgoing steps retained in history, and new
   increments afterward. Repeat asleep and with an active timer alert. Reset
   after the new baseline anchor is saved and verify both dates survive,
   without waiting for a 30-minute checkpoint.
4. Exercise same-date time corrections, another retained date, an unrecorded
   date, and a correction more than 14 days backward. Inspect the saved history
   to confirm date uniqueness and that the retained 15-calendar-date window
   advances correctly. Test month, year, and leap-day boundaries.
5. Restart without CTS and verify the saved checkpoint clock fallback, then
   resynchronize. Check failed reads and saves retain known counts and retry
   without a tight loop. Test a truncated/corrupted blob in a controlled setup;
   boot should reject it and start with the normal compile-time fallback.
6. Remove sensor power and reboot to verify saved-total fallback with a fresh
   raw baseline. Test a version-1 history upgrade. In a controlled failure
   setup, interrupt or fail the new anchor save: `CTRL3_C` must remain `0x04`,
   and reboot must not add a stale raw-counter delta. Confirm pending anchor
   retries are at most once per minute and successful commit permits `0x44`.
