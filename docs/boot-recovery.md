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
