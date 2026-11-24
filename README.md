# OpenComfort

[![License: GPLv3 Non-Commercial](https://img.shields.io/badge/License-GPLv3--NC-blue.svg)](LICENSE.md)
[![Commercial Use Requires License](https://img.shields.io/badge/Commercial%20Use-License%20Required-orange.svg)](COMMERCIAL_LICENSE.md)
![Platform: ESP32-S3](https://img.shields.io/badge/Platform-ESP32--S3-red.svg)
![ESP-IDF 5.5.1](https://img.shields.io/badge/ESP--IDF-5.5.1-informational.svg)

**Local, cloud-free control of your air conditioner, on an ESP32-S3.**

OpenComfort is modular ESP-IDF firmware for smart-home products. A product definition flashed into the image selects the module and the board; the first module is an air-conditioner bridge. Each device serves its own authenticated web portal and can also join Home Assistant over MQTT or, on 16 MB hardware, Matter.

> **Status: early development.** The firmware builds and the core flows are in place. Hardware behaviour must be verified on a board before release.

## Features

- **Guided setup** over a WPA2 captive portal: Wi-Fi from a scan list, AC make and model, administrator password, optional connectors.
- **Profile-driven protocols.** AC behaviour comes from JSON profiles. Remote profiles are verified with SHA-256 and an ECDSA P-256 signature.
- **One runtime, several control surfaces.** The local portal, Home Assistant MQTT discovery and Matter all go through one serialized runtime.
- **Hardened transports** for UART, SPI and CAN with pins fixed by the board definition.
- **Durable configuration** in NVS with dual-slot commits and schema migration.
- **Factory reset** by holding the BOOT button.
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
| [AC Profiles](docs/profiles.md) | Catalog and profile format, command encoding, signing, adding a unit |
| [Runtime API](docs/runtime-api.md) | HTTP contract, Home Assistant MQTT, Matter build and manufacturing |
| [Security](SECURITY.md) | Reporting vulnerabilities, production hardening, profile signing |
| [Contributing](CONTRIBUTING.md) | Workflow, coding guidelines, contribution terms |

## Supported hardware

| Air conditioner | Bus | Profile |
|---|---|---|
| Mitsubishi Heavy Industries (SRK25ZSX, SRK35ZSX, generic) | SPI | [`mhi.json`](main/spiffs/profiles/mhi.json) |

Your unit isn't listed? [Add a profile](docs/profiles.md#adding-an-air-conditioner).

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
│   ├── Modules/          Modules and the AC protocol engine
│   ├── Runtime/          Serialized state and command API
│   └── Connectors/       Web, Home Assistant MQTT, Matter
├── spiffs/               Portal UI and bundled profiles
└── test/                 Unity tests
matter_components/        Isolated ESP-Matter component
scripts/                  Toolchain bootstrap and profile signing
```

## Contributing

Bug reports, profiles and pull requests are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) first; it covers the workflow and the contribution licence terms.

## License

OpenComfort is free for **personal, educational and non-commercial use** under [LICENSE.md](LICENSE.md). **Commercial use**, including embedding the firmware in products, paid services or managed smart-home systems, requires a separate licence. See [COMMERCIAL_LICENSE.md](COMMERCIAL_LICENSE.md).

OpenComfort is an independent project and is not affiliated with or endorsed by any air-conditioner manufacturer.
