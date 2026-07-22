# OpenComfort

[![License: GPLv3 Non-Commercial](https://img.shields.io/badge/License-GPLv3--NC-blue.svg)](LICENSE.md)
[![Commercial Use Requires License](https://img.shields.io/badge/Commercial%20Use-License%20Required-orange.svg)](COMMERCIAL_LICENSE.md)

OpenComfort is ESP-IDF firmware for an ESP32-S3 comfort controller and AC
bridge.

## Development setup

The project uses ESP-IDF v5.5.1 and targets `esp32s3`.

### Devcontainer

The recommended setup is the included development container:

1. Install Docker and the Dev Containers extension in Cursor or VS Code.
2. Open this repository.
3. Run **Dev Containers: Reopen in Container**.

The container includes the ESP-IDF toolchain and configures the project target
after creation.

### Linux host

If a container is unavailable, install the SDK and toolchain under your user
account:

```bash
./scripts/bootstrap-dev.sh
source "$HOME/.espressif/frameworks/esp-idf-v5.5.1/export.sh"
idf.py set-target esp32s3
```

Set `OPENCOMFORT_IDF_PATH` before running the script to choose another install
directory. Set `IDF_VERSION` only when intentionally testing another ESP-IDF
release.

## Build

```bash
idf.py build
```

Build artifacts and the generated `sdkconfig` are intentionally ignored.

Flashing requires a connected ESP32-S3 and an explicit serial port:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Project structure

- `main/main.cpp` contains the firmware entry point.
- `main/src/Busses/` provides UART, SPI, and CAN transports.
- `main/src/Modules/` contains device modules and factories.
- `main/src/Configs/` handles device and persisted configuration.
- `main/src/Http/` contains the captive portal.
- `main/spiffs/` contains files packaged into SPIFFS.

See [CONTRIBUTING.md](CONTRIBUTING.md) before submitting changes.
