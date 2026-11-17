"""Use the ESP-IDF DOUT bootloader instead of the framework's QIO template."""

from pathlib import Path

Import("env")

source = Path(env.subst("$PROJECT_DIR")) / "bootloader" / "hacktor_dout40.bin"
target = Path(env.subst("$BUILD_DIR")) / "bootloader.bin"
image = source.read_bytes()
target.parent.mkdir(parents=True, exist_ok=True)
if not target.exists() or target.read_bytes() != image:
    target.write_bytes(image)

# The framework accepts an absolute custom bootloader path. Keep a copy in the
# standard build directory so both PlatformIO upload and manual recovery agree.
env.BoardConfig().update("build.arduino.custom_bootloader", str(target.resolve()))
