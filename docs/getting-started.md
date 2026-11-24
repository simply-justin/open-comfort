# Getting Started

This guide takes you from a fresh clone to a provisioned OpenComfort device that you control from your browser.

## Prerequisites

| Requirement | Notes |
|---|---|
| ESP-IDF v5.5.1 | Installed by the devcontainer or `scripts/bootstrap-dev.sh` |
| ESP32-S3 board | 4 MB flash for the default build, 16 MB for the Matter build |
| USB data cable | For flashing and the serial monitor |
| 2.4 GHz Wi-Fi network | Needed during setup |
| OpenComfort hardware or wiring | The bus pins are fixed by the board definition (see [Architecture](architecture.md#board-definition)) |

## 1. Set up the toolchain

**Devcontainer (recommended).** Open the repository in VS Code or Cursor and run **Dev Containers: Reopen in Container**. The container installs ESP-IDF and selects the `esp32s3` target.

**Debian or Ubuntu host.**

```sh
sudo apt-get update
sudo apt-get install -y \
  bison ccache cmake dfu-util flex git gperf libffi-dev libssl-dev \
  libusb-1.0-0-dev ninja-build python3 python3-pip python3-venv wget
./scripts/bootstrap-dev.sh
source "$HOME/.espressif/frameworks/esp-idf-v5.5.1/export.sh"
idf.py set-target esp32s3
```

`bootstrap-dev.sh` installs ESP-IDF in your home directory and does not change the repository.

## 2. Build

```sh
idf.py build
```

This builds the default **legacy 4 MB** profile (local web portal and Home Assistant MQTT). `sdkconfig.defaults` already selects the custom partition table, the flash size and the HTTPS certificate bundle, so no `menuconfig` changes are needed.

For the **Matter 16 MB** profile, see [Runtime API › Product profiles](runtime-api.md#product-profiles).

## 3. Flash

```sh
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with your board's serial port. Press `Ctrl+]` to leave the monitor. The build also flashes the SPIFFS image with the portal UI and the bundled AC profile catalog.

## 4. First-time setup

A device without a valid configuration starts the captive portal:

1. Join the Wi-Fi network **`OpenComfort-XXXX`** (the last four hex digits of the MAC address). The password is `CONFIG_OPENCOMFORT_PORTAL_AP_PASSWORD`, `opencomfort` by default.
2. Your phone or laptop should open the setup page by itself. If it does not, browse to `http://192.168.4.1`.
3. Work through the three steps:
   1. **Network.** Pick your Wi-Fi from the scan list and enter its password. Use **Other…** for a hidden network.
   2. **Air conditioner.** Pick your model and give the device a name.
   3. **Secure and connect.** Set the portal password and optionally enable Home Assistant MQTT or Matter.
4. Tap **Finish setup**. The device saves the configuration, shows its address and restarts.

## 5. Open the runtime portal

After the restart the device joins your network and advertises itself over mDNS:

```text
http://opencomfort-xxxx.local
```

On networks without mDNS, use the IP address from your router's client list or the serial log. Sign in with the administrator password to control the unit and change settings. See [Runtime API](runtime-api.md) for everything the portal and API can do.

## Factory reset

Hold the factory-reset button (GPIO0, the BOOT button on rev-A boards) for 5 seconds, at power-on or while running. This clears the user configuration, Wi-Fi credentials, connector secrets and Matter fabrics, then restarts into the captive portal. The hold time is `CONFIG_OPENCOMFORT_FACTORY_RESET_HOLD_MS`.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `idf.py: command not found` | ESP-IDF is not loaded in this shell | `source "$HOME/.espressif/frameworks/esp-idf-v5.5.1/export.sh"` |
| No `OpenComfort-XXXX` network | The device already has a valid configuration | Do a factory reset |
| Setup page does not open | Captive-portal detection is disabled on the client | Browse to `http://192.168.4.1` |
| `STA connect failed at boot` in the log | Network out of range or credentials changed | Check the network, or factory reset and run setup again |
| `opencomfort-xxxx.local` does not resolve | The network or client does not support mDNS | Use the IP address instead |
| Model missing from the list | No profile for it in the catalog | See [AC Profiles](profiles.md#adding-an-air-conditioner) |

## Hardware safety

Air-conditioner indoor units can expose mains voltage close to their control connectors.

- Switch the unit off at the breaker before connecting anything.
- ESP32-S3 GPIOs are 3.3 V only. Use level shifting or isolation where the unit needs it.
- Confirm the pinout for your specific model.
- Opening or modifying your unit may void its warranty. You are responsible for your own installation.
