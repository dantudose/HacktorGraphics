# Daily steps

Steps and estimated distance restart at **00:00 on the watch's displayed
calendar date**, on every face and the status screen. The IMU keeps its running
hardware counter. A successful midnight snapshot adds the last unsampled steps
to the outgoing date, including steps taken while the display was asleep, then
becomes the new date's baseline. The daily total uses 32 bits and handles the
hardware counter's 16-bit rollover when fewer than 65,536 steps pass between
samples.

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
have changed since the last successful save. Unchanged totals on the same date
do not write, even just to refresh a timestamp. The first checkpoint is a full
30 minutes after boot. Success, failure, and an unchanged snapshot all start a
new 30-minute interval; delayed service does not trigger catch-up writes.
Midnight finalizes history in RAM and waits for the next checkpoint to persist
it. Clock edits and baseline retries also cannot bypass that interval. Step
history therefore attempts at most 48 flash saves per day, often fewer.

The entire snapshot is one **144-byte**, versioned, little-endian blob with
CRC-32, stored under `steps-v1` in the existing `watch` NVS namespace. This uses
the ESP32's main flash, independently of the external NOR chip on the switched
peripheral rail. Loading never writes. Missing, truncated, unsupported, or
invalid snapshots are rejected without partially restoring their contents.
Failed saves retain the RAM changes for the next 30-minute checkpoint.

NVS appends replacement values to its log and reclaims pages, distributing
flash use; a checkpoint does not imply a sector erase. Espressif documents
recovery after power loss, with the value being written potentially lost. The
CRC checks the application snapshot as well. These mechanisms do not guarantee
a particular flash lifetime or preserve unsaved RAM data. See
[Espressif's NVS storage and robustness documentation](https://docs.espressif.com/projects/esp-idf/en/v5.1.4/esp32s3/api-reference/storage/nvs_flash.html).

An abrupt reset normally risks the most recent **30 minutes** of steps and
distance. Failed sensor reads, failed writes, or delayed service can extend
that window. On boot, firmware restores the saved date's daily total and takes
a fresh sensor baseline after IMU initialization. It never reuses a raw sensor
value from before reset, which avoids treating an IMU reset as a large rollover.

The persisted checkpoint date/time also supplies the boot clock fallback when
valid, preventing the firmware build date from replacing the saved activity
date. Without a valid snapshot, the compile-time fallback is used. There is no
persistent RTC to measure elapsed time across reset or power loss; CTS or manual
time setting must correct that fallback. A stationary watch can retain an old
checkpoint time because unchanged totals do not justify a flash write.

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
c++ -std=c++11 -Wall -Wextra -Werror -I test/step_stubs -I include test/test_step_counter_read.cpp -o /tmp/hacktor-step-counter-read-tests
/tmp/hacktor-step-counter-read-tests
pio run -e hacktor_watch_s3_lgfx
```

Host tests cover dated retention, reset restoration with a fresh sensor
baseline, rollover/wrap, malformed and corrupted blobs, failed writes, unchanged
save suppression, 30-minute scheduling and millisecond rollover. They do not
measure hardware power or emulate NVS power interruption.

On the watch:

1. Walk, let a checkpoint pass, record the steps/distance, then reset. Verify
   that the saved count returns on Analog, Digital, and status and that walking
   afterward adds normally. Reset before the next checkpoint to verify that
   only the unsaved portion is lost. Reset several times before 30 minutes;
   history must not write at each boot.
2. Repeat on battery with wrist wake off, walking while the screen is asleep
   through a checkpoint. Check for potential brief rail glow, preserved face
   and sleep state, and restored asleep steps after reset. Leave it stationary
   across later checkpoints and check logs/instrumentation for no history
   write when counts are unchanged. Measure the added checkpoint energy.
3. Set the clock shortly before midnight using CTS or Settings. Verify zero
   steps/distance at rollover, outgoing steps retained in history, and new
   increments afterward. Repeat asleep and with an active timer alert. Reset
   after the next checkpoint and verify both dates survive.
4. Exercise same-date time corrections, another retained date, an unrecorded
   date, and a correction more than 14 days backward. Inspect the saved history
   to confirm date uniqueness and that the retained 15-calendar-date window
   advances correctly. Test month, year, and leap-day boundaries.
5. Restart without CTS and verify the saved checkpoint clock fallback, then
   resynchronize. Check failed reads and saves retain known counts and retry
   without a tight loop. Test a truncated/corrupted blob in a controlled setup;
   boot should reject it and start with the normal compile-time fallback.
