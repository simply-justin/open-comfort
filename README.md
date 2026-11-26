# OpenComfort

[![License: GPLv3 Non-Commercial](https://img.shields.io/badge/License-GPLv3--NC-blue.svg)](LICENSE.md)
[![Commercial Use Requires License](https://img.shields.io/badge/Commercial%20Use-License%20Required-orange.svg)](COMMERCIAL_LICENSE.md)
![Platform: ESP32-S3](https://img.shields.io/badge/Platform-ESP32--S3-red.svg)
![ESP-IDF 5.5.1](https://img.shields.io/badge/ESP--IDF-5.5.1-informational.svg)

**A modular, cloud-free firmware platform for smart-home devices on the ESP32-S3.**

OpenComfort provides everything a local smart-home device needs: guided setup, a secure web portal, Home Assistant MQTT and Matter, signed over-the-air updates, schedules, logging and safe storage. What the device actually does lives in a **module**. A product definition selects the module and the board at build time, so the same platform can drive different kinds of hardware.

The first module bridges air conditioners. More modules can be added without touching the platform.

> **Status: early development.** The firmware builds and the core flows are in place. Hardware behaviour must be verified on a board before release.

## Platform features

Every module gets these:

- **Guided setup** over a WPA2 captive portal: Wi-Fi from a scan list, the module's own setup steps, an administrator password and optional connectors.
- **One runtime, several control surfaces.** The local portal, Home Assistant MQTT discovery and Matter all go through one serialized runtime, so only one task ever talks to the hardware.
- **A secure local portal** with sessions, CSRF tokens, per-client login throttling and DNS-rebinding protection.
- **Signed firmware updates** from GitHub releases (release or pre-release channel, optional automatic install) or from a file, with automatic rollback if a new image fails to start.
- **Hardened transports** for UART, SPI and CAN with pins fixed by the board definition.
- **Signed, downloadable device profiles.** Vendor protocols are JSON profiles, not code. They are published from this repository and downloaded by the device, so a new profile needs no firmware build; each is verified with SHA-256 and an ECDSA P-256 signature.
- **Durable configuration** in NVS with dual-slot commits and schema migration.
- **Schedules, logs and history** on the device, with logs that survive a crash.
- **Factory reset** by switching the power off and on 5 times, or by holding the BOOT button.
- **Production security prep** for Secure Boot, flash encryption and NVS encryption.

## Quick start

```sh
./scripts/bootstrap-dev.sh
source "$HOME/.espressif/frameworks/esp-idf-v5.5.1/export.sh"
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Then join the `OpenComfort-XXXX` Wi-Fi network (password `opencomfort`) and follow the setup page. The devcontainer is the easiest way to get a working toolchain. See **[Getting Started](docs/getting-started.md)** for the full walkthrough.

## Documentation

| Guide | What's inside |
|---|---|
| [Getting Started](docs/getting-started.md) | Toolchain, build, flash, first setup, factory reset, troubleshooting |
| [Architecture](docs/architecture.md) | Boot flow, components, runtime and connectors, board pins, partitions |
| [Configuration](docs/configuration.md) | Build-time options, what setup stores, where secrets live |
| [AC Profiles](docs/profiles.md) | AC module: catalog and profile format, command encoding, signing, adding a unit |
| [Runtime API](docs/runtime-api.md) | HTTP contract, Home Assistant MQTT, Matter build and manufacturing |
| [Security](SECURITY.md) | Reporting vulnerabilities, production hardening, profile signing |
| [Contributing](CONTRIBUTING.md) | Workflow, coding guidelines, contribution terms |

## Modules

| Module | What it does | Supported devices |
|---|---|---|
| AC bridge | Power, mode, setpoint, fan and vanes; reads room temperature and state back | Mitsubishi Heavy Industries (SRK25ZSX, SRK35ZSX, generic) over SPI, profile [`mhi.json`](profiles/mhi.json) |

Your air conditioner isn't listed? [Add a profile](docs/profiles.md#adding-an-air-conditioner). A new kind of device? [Add a module](docs/architecture.md#extending-the-firmware).

## Project structure

```text
main/
├── main.cpp              Entry point
├── src/
│   ├── Application/      Boot orchestration
│   ├── Product/          Product and board definitions
│   ├── Config/           NVS user config, secrets, factory reset
│   ├── Wifi/             SoftAP and station
│   ├── Http/             Captive portal
│   ├── Setup/            Module setup providers
│   ├── Profiles/         Catalog, crypto, HTTPS download
│   ├── Bus/              UART, SPI and CAN transports
│   ├── Modules/          Modules (AC bridge and its protocol engine)
│   ├── Runtime/          Serialized state and command API
│   └── Connectors/       Web, Home Assistant MQTT, Matter
├── www/                  Portal UI (embedded in the app)
└── test/                 Unity tests
matter_components/        Isolated ESP-Matter component
test_app/                 Unity test app (runs in QEMU)
profiles/                 AC profiles, signed and published by CI
scripts/                  Toolchain bootstrap, unit tests, profile catalog
```

## Contributing

Bug reports, profiles and pull requests are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) first; it covers the workflow and the contribution licence terms.

## License

OpenComfort is free for **personal, educational and non-commercial use** under [LICENSE.md](LICENSE.md). **Commercial use**, including embedding the firmware in products, paid services or managed smart-home systems, requires a separate licence. See [COMMERCIAL_LICENSE.md](COMMERCIAL_LICENSE.md).

OpenComfort is an independent project and is not affiliated with or endorsed by any device manufacturer.
