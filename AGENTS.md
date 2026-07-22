# OpenComfort agent guide

OpenComfort is ESP-IDF C++ firmware for the ESP32-S3. Keep changes focused,
portable, and safe for hardware.

## Development environment

ESP-IDF v5.5.1 is the supported SDK version.

Preferred setup:

1. Open the repository in Cursor or VS Code.
2. Run **Dev Containers: Reopen in Container**.
3. The container installs ESP-IDF and configures the `esp32s3` target.

On a Linux host without the devcontainer:

```bash
./scripts/bootstrap-dev.sh
source "$HOME/.espressif/frameworks/esp-idf-v5.5.1/export.sh"
idf.py set-target esp32s3
```

The bootstrap script installs SDK tools in the user's home directory; it does
not modify the repository.

## Validation

Run from the repository root:

```bash
idf.py build
```

Do not claim a hardware behavior was verified unless it was tested on an
ESP32-S3 board. Flashing and serial monitoring require an explicitly selected
device:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Never flash a connected device as part of routine validation.

## Repository map

- `main/main.cpp`: firmware entry point.
- `main/src/Busses/`: UART, SPI, and CAN abstractions.
- `main/src/Modules/`: module implementations and factories.
- `main/src/Configs/`: runtime and persisted configuration.
- `main/src/Http/`: captive portal.
- `main/spiffs/`: files packaged into the SPIFFS image.

When adding source files, register them in `main/CMakeLists.txt`. Keep hardware
pins and protocol values in configuration rather than embedding board-specific
values in reusable modules.

## Change guidelines

- Follow the existing C++ style and ESP-IDF APIs.
- Check every ESP-IDF return value that can affect safe operation.
- Avoid blocking indefinitely in FreeRTOS tasks.
- Document changes to pin assignments, UART/SPI/CAN settings, partitions, or
  persisted configuration.
- Do not add secrets, Wi-Fi credentials, serial device names, generated
  `sdkconfig`, or files under `build/`.
- Contributions must comply with `CONTRIBUTING.md` and the repository's
  licensing terms.
