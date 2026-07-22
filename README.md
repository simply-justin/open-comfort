# OpenComfort

[![License: GPLv3 Non-Commercial](https://img.shields.io/badge/License-GPLv3--NC-blue.svg)](LICENSE.md)
[![Commercial Use Requires License](https://img.shields.io/badge/Commercial%20Use-License%20Required-orange.svg)](COMMERCIAL_LICENSE.md)

OpenComfort is modular ESP-IDF firmware for ESP32-S3 smart-home products.
A flashed product definition selects the active module. The first module is
an air-conditioner bridge.

## Current functionality

1. Immutable product/module identity from Kconfig.
2. WPA2 captive portal for first-time setup (Wi-Fi first, then module steps).
3. Durable NVS user configuration with dual-slot commits.
4. AC make/model selection from a bundled catalog.
5. Optional remote HTTPS profile catalog download with SHA-256 + ECDSA verification.
6. Hardened UART / SPI / CAN transports with validation and RAII cleanup.
7. AC protocol engine that encodes profile commands and polls data registers.
8. Factory reset / re-provisioning via long-press on the board reset GPIO.
9. Production security prep files for Secure Boot, flash encryption, and NVS encryption.
10. A serialized runtime API shared by an authenticated local portal, Home
    Assistant MQTT discovery, and an optional Matter connector.
11. Legacy 4 MB and Matter-capable 16 MB firmware profiles.

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

The default build targets legacy 4 MB hardware. See
[docs/runtime-api.md](docs/runtime-api.md) for the isolated 16 MB Matter build,
connector setup, runtime endpoints, and manufacturing requirements.

Flashing requires a connected ESP32-S3 and an explicit serial port:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Default SoftAP credentials for first-time setup:

- SSID: `OpenComfort-XXXX`
- Password: value of `CONFIG_OPENCOMFORT_PORTAL_AP_PASSWORD` (default `opencomfort`)

Factory reset: hold GPIO0 (BOOT) for 5 seconds.

See [SECURITY.md](SECURITY.md) for production hardening and profile signing.

## Project structure

- `main/main.cpp`: firmware entry point
- `main/src/Application/`: boot orchestration
- `main/src/Product/`: immutable product and board definitions
- `main/src/Config/`: durable NVS user configuration and factory reset
- `main/src/Wifi/`: SoftAP / STA service
- `main/src/Http/`: captive portal
- `main/src/Runtime/`: serialized device state and command API
- `main/src/Connectors/`: local web, Home Assistant MQTT, and Matter adapters
- `main/src/Setup/`: module setup providers
- `main/src/Profiles/`: AC profile catalog, crypto, HTTPS download
- `main/src/Bus/`: hardened UART / SPI / CAN transports
- `main/src/Modules/`: runtime modules and AC protocol engine
- `main/spiffs/www/`: portal UI assets
- `main/spiffs/profiles/`: bundled AC catalog and profiles

See [CONTRIBUTING.md](CONTRIBUTING.md) before submitting changes.
