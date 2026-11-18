# Boot recovery

The reported `Invalid image block, can't boot` error occurred in ESP32-S3 ROM
loading, before application execution. The ROM misread the second bootloader
segment in DIO mode. DOUT/40 MHz loaded it correctly, but the framework's bundled
bootloader then enabled QIO internally and reset before entering the app.

The project now uses the [source-built DOUT bootloader](../bootloader/README.md)
and DOUT/40 MHz application image headers. A normal PlatformIO upload writes:

| Offset | Image |
| --- | --- |
| `0x0000` | DOUT bootloader |
| `0x8000` | Partition table |
| `0xe000` | OTA boot-selection metadata |
| `0x10000` | Application |

These writes leave the NVS settings partition at `0x9000`–`0xdfff` untouched.
No full-chip erase or eFuse changes were needed. Initial all-`0xff` reads during
the failure are not sufficient evidence that settings were erased: read-mode
problems can also produce invalid data.

Stop the serial monitor before uploading:

```sh
pio run -e hacktor_watch_s3_lgfx -t upload --upload-port /dev/cu.usbmodem101
pio device monitor --port /dev/cu.usbmodem101 --baud 115200
```

A successful bootloader startup logs `SPI Mode : DOUT`, `Boot SPI Speed : 40MHz`,
the partition table, and application loading from `0x10000`. Application logging
now defaults to off; set `HACKTOR_LOG_LEVEL=4` or `5` in `platformio.ini` and
rebuild to see `Booting...`, I²C detection, and IMU/time-sync diagnostics. See
[serial logging](logging.md). Repeated ROM banners mean
startup is still failing; a successful build/upload alone is not a runtime test.

Warm resets can also leave the IMU in its embedded register bank because its
supply stays on. Startup now restores `FUNC_CFG_ACCESS=0` before reading its
identity. The observed `0x10` identity was the default value of bank A's
pedometer-threshold register at the same address, rather than the normal
`WHO_AM_I=0x6a`; see the [LSM6DSL register map](https://www.st.com/resource/en/datasheet/lsm6dsl.pdf).

Startup keeps touch in reset and checks the IMU before loading step history.
It verifies 13 expected control/interrupt registers and reads the raw step
counter. If the running configuration and committed-anchor marker are valid,
it skips `SW_RESET` and reconfiguration, loads the saved daily/raw reference,
and adds the live counter delta. A normal warm reset can therefore recover
steps that have not reached a 30-minute flash checkpoint, without forcing a
new history write.

Cold or unverified sensors retain the known-good reset/setup sequence. Startup
restores the saved daily count, takes a fresh raw baseline, and saves the new
anchor once after boot time synchronization has had a chance to set the date.
Legacy 144-byte version-1 history loads but has no raw anchor; it is upgraded
to a 148-byte version-2 snapshot under the same NVS key at that baseline save.
Unsaved pre-upgrade activity and unsaved activity lost with sensor power cannot
be recovered from the old counter reference.

The firmware uses `CTRL3_C.BDU` as a committed-anchor gate: `0x04` remains in
place during cold/day-baseline changes, and only a successful flash commit
permits `0x44`, which warm startup requires. This prevents an interrupted setup
from pairing an old flash reference with a new sensor/day baseline. BDU changes
unused accelerometer/gyro output-byte latching; step counting stays enabled.
This relies on the current no-FIFO configuration. See
[ST AN5040, sections 4.4, 5.7 and 6.1](https://www.st.com/resource/en/application_note/dm00402563.pdf)
and [daily-step recovery and validation](daily-steps.md).
