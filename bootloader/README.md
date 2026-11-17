# Hacktorwatch DOUT bootloader

`hacktor_dout40.bin` is an ESP32-S3 second-stage bootloader built from ESP-IDF
5.1.4.240602 with DOUT, 40 MHz, 8 MB flash, partition table offset `0x8000`, and
INFO startup logging. Secure boot and flash encryption are disabled. It is
20,944 bytes and fits below the partition table. Its configuration is retained
in `sdkconfig.defaults`.

SHA-256:

```text
064eb631633f55ddadd04f717785cc6fe02d1cdc6d958fff15a9bfcab4ae55ee
```

The framework's QIO bootloader enables quad reads internally, even when its
image header is changed to DOUT. On this watch, DIO caused ROM segment reads
to fail and the QIO bootloader reset before application startup. This loader
keeps DOUT enabled through application loading. Recovery was verified on the
connected watch, including normal application initialization.

`tools/use_watch_bootloader.py` copies this image to the standard build output
and selects it through the framework's custom bootloader option. Normal
`pio run -e hacktor_watch_s3_lgfx -t upload` includes it automatically.
The application SDK variant stays `qio_qspi` because that is the installed
Arduino framework's library layout; it does not select the bootloader here.

To rebuild, activate a matching ESP-IDF 5.1.4 environment with its Python
dependencies, CMake, Ninja, and ESP32-S3 toolchain installed. From the project:

```sh
watch_boot_build=/tmp/hacktor-dout-bootloader-rebuild
mkdir -p "$watch_boot_build"
cp bootloader/sdkconfig.defaults "$watch_boot_build/sdkconfig"
cmake -S "$IDF_PATH/components/bootloader/subproject" -B "$watch_boot_build" -G Ninja \
  -DIDF_PATH="$IDF_PATH" -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG="$watch_boot_build/sdkconfig" \
  -DPYTHON="$(command -v python)" -DIDF_COMPONENT_MANAGER=0
cmake --build "$watch_boot_build" --parallel 6
```

The resulting `bootloader.bin` and `bootloader.elf` are in that build directory.
Inspect the image with esptool and confirm DOUT/40 MHz/8 MB before replacing
the checked-in binary. Build timestamps can change the rebuilt hash.

ESP-IDF is distributed under the [Apache License 2.0](https://github.com/espressif/esp-idf/blob/v5.1.4/LICENSE).
