# OpenComfort

[![License: GPLv3 Non-Commercial](https://img.shields.io/badge/License-GPLv3--NC-blue.svg)](LICENSE.md)
[![Commercial Use Requires License](https://img.shields.io/badge/Commercial%20Use-License%20Required-orange.svg)](COMMERCIAL_LICENSE.md)

OpenComfort is modular ESP-IDF firmware for ESP32-S3 smart-home products.
A flashed product definition selects the active module. The first module is
an air-conditioner bridge.

## What the MVP does

1. Boots with an immutable product/module identity from Kconfig.
2. Starts a WPA2 captive portal when the device is unprovisioned.
3. Always configures Wi-Fi first.
4. Continues with module-specific setup (AC make/model from a bundled catalog).
5. Stores durable user configuration in NVS.
6. Activates the protocol bus required by the selected AC profile.

## Development setup

The project uses ESP-IDF v5.5.1 and targets `esp32s3`.

### Devcontainer

1. Install Docker and the Dev Containers extension in Cursor or VS Code.
2. Open this repository.
3. Run **Dev Containers: Reopen in Container**.

### Linux host

```bash
sudo apt-get update
sudo apt-get install -y \
  bison ccache cmake dfu-util flex git gperf libffi-dev libssl-dev \
  libusb-1.0-0-dev ninja-build python3 python3-pip python3-venv wget
./scripts/bootstrap-dev.sh
source "$HOME/.espressif/frameworks/esp-idf-v5.5.1/export.sh"
idf.py set-target esp32s3
```

## Build

```bash
idf.py build
```

Flashing requires a connected ESP32-S3 and an explicit serial port:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Default SoftAP credentials for first-time setup:

- SSID: `OpenComfort-XXXX`
- Password: value of `CONFIG_OPENCOMFORT_PORTAL_AP_PASSWORD` (default `opencomfort`)

## Project structure

- `main/main.cpp`: firmware entry point
- `main/src/Application/`: boot orchestration
- `main/src/Product/`: immutable product and board definitions
- `main/src/Config/`: durable NVS user configuration
- `main/src/Wifi/`: SoftAP / STA service
- `main/src/Http/`: captive portal
- `main/src/Setup/`: module setup providers
- `main/src/Profiles/`: AC profile catalog and loader
- `main/src/Bus/`: hardened UART / SPI / CAN transports
- `main/src/Modules/`: runtime modules
- `main/spiffs/www/`: portal UI assets
- `main/spiffs/profiles/`: bundled AC catalog and profiles

See [CONTRIBUTING.md](CONTRIBUTING.md) before submitting changes.
