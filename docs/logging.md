# Serial logging

Set the single build-time switch in `platformio.ini`, then rebuild and upload:

```ini
  -D HACKTOR_LOG_LEVEL=0
```

The default is **0**, for production without application serial output. Levels
include all messages at lower severities:

| Level | Messages |
| --- | --- |
| 0 | Off |
| 1 | Errors |
| 2 | Errors and warnings |
| 3 | Info, including boot, time sync and timer completion |
| 4 | Debug, including startup I²C scan, IMU setup and CTS discovery |
| 5 | Verbose, including IMU interrupt traces and the two-second diagnostic line |

`CORE_DEBUG_LEVEL=HACKTOR_LOG_LEVEL` makes Arduino diagnostics and NimBLE's C++
logs follow the same setting. NimBLE's lower-level host logging remains at its
existing off default. The installed framework's prebuilt SDK also defaults to
logging off; enabling application logs does not rebuild that SDK.

Application messages use `WATCH_LOG_E/W/I/D/V` from `include/WatchLog.hpp`. Each
macro accepts a format string literal and optional printf arguments, prefixes
the severity, and appends a newline. Disabled messages, their strings, and their
argument expressions are removed at compile time. Keep required operations
outside log arguments.

At level 0, startup skips the two-second serial-console wait. Below level 4,
startup also skips the diagnostic I²C scan and pedometer register dump. Below
level 5, the periodic diagnostic block is absent. Required sensor initialization,
interrupt acknowledgement, display rendering, timer handling, and vibration
continue at every level.

USB initialization and host detection stay enabled so uploads remain available.
As before, a connected USB host keeps the processor awake; measure battery
runtime with USB disconnected. Disabling logs avoids formatting, serial writes,
and diagnostic activity, but the runtime improvement needs a current measurement.

ROM startup, the separately compiled DOUT bootloader, crashes, and assertions
are outside this application switch and may still print. The working bootloader
and its flash configuration are retained. To inspect full application startup
after a boot problem, select level 4 or 5 before rebuilding.

## Validation

The native logger tests check filtering, formatting, unevaluated disabled
arguments, and use in unbraced conditionals at every level:

```sh
for level in 0 1 2 3 4 5; do
  c++ -std=c++11 -Wall -Wextra -Werror -DHACKTOR_LOG_LEVEL=$level \
    -I test/log_stubs -I include test/test_watch_log.cpp -o /tmp/hacktor-watch-log-$level
  /tmp/hacktor-watch-log-$level
done
```

Build firmware at levels 0 and 5 to cover both the production and diagnostic
paths. On hardware, check normal boot/wake and uploads with level 0, then level
5 output and watch controls, timer expiry and vibration with the monitor open.
