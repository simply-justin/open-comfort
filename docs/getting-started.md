# Getting Started

This guide takes you from a fresh clone to a running OpenComfort device that you can control from your browser.

**Time required:** about 30 minutes, plus the time to install ESP-IDF if you don't have it yet.

## Prerequisites

| Requirement | Version / notes |
|---|---|
| [ESP-IDF](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/) | 5.x (the project was created with 5.5.1) |
| Target board | ESP32-S3 with at least 2 MB flash |
| USB cable | Data-capable, for flashing and the serial monitor |
| Wi-Fi network | 2.4 GHz, needed during setup to download the device profile |
| Interface hardware | Wiring or an adapter between the ESP32-S3 and your air conditioner (see [Hardware safety](#hardware-safety)) |

> **Prefer containers?** The `.devcontainer/` folder provides a ready-made ESP-IDF environment for VS Code Dev Containers. Open the repository in VS Code and choose **Reopen in Container**.

## 1. Clone and prepare the environment

```sh
git clone https://github.com/simply-justin/open-comfort.git
cd open-comfort

# Load ESP-IDF into the current shell (the path depends on your installation)
. "$IDF_PATH/export.sh"
```

Verify that the toolchain is available:

```sh
idf.py --version
```

## 2. Configure the project

`sdkconfig` is not committed to the repository, so a fresh clone starts from ESP-IDF defaults. Set the target first:

```sh
idf.py set-target esp32s3
```

Then open the configuration menu:

```sh
idf.py menuconfig
```

Set the following options. Without them the firmware cannot mount its storage partition:

| Menu path | Value |
|---|---|
| **Partition Table → Partition Table** | `Custom partition table CSV` |
| **Partition Table → Custom partition CSV file** | `partitions.csv` |
| **Serial flasher config → Flash size** | `2 MB` (or the size of your board) |
| **Component config → mbedTLS → Certificate Bundle** | Enabled (the default; needed for HTTPS profile downloads) |

Save and exit.

## 3. Build, flash, and monitor

```sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Replace `/dev/ttyUSB0` with your board's serial port. On macOS this is usually `/dev/cu.usbmodem*`, and on Windows `COMx`. Press `Ctrl+]` to exit the monitor.

The build also packs `main/spiffs/` into a SPIFFS image and flashes it to the `storage` partition. This image contains the bundled manifest and example profile.

## 4. Run the first-time setup

On first boot there is no configuration yet, so the device starts in **setup mode**:

1. The serial log shows `Starting setup portal`.
2. On your phone or laptop, join the open Wi-Fi network **`OpenComfort-Setup`**.
3. Open **<http://192.168.4.1>** in a browser. The setup network does not redirect you automatically, so type the address yourself.
4. Fill in the setup wizard:
   - Your home Wi-Fi SSID and password.
   - The product (air-conditioner model) from the list.
   - The bus pins that connect the ESP32-S3 to your unit.
5. Submit. The device joins your Wi-Fi, downloads and validates the profile, caches it, and saves its configuration.

When setup succeeds, the device leaves setup mode and starts normally. For details of each step, see [Configuration](configuration.md#first-time-setup).

## 5. Verify it works

Find the device's IP address in the serial log or in your router's client list. Then check:

- [ ] `http://<device-ip>/` shows the AC control page.
- [ ] `curl http://<device-ip>/api/status` returns `{"device":"…","configured":true}`.
- [ ] The serial log shows `AC module booted with profile …` and `Entering main loop`.

## Resetting the device

To run setup again, send:

```sh
curl -X POST http://<device-ip>/api/setup/reset
```

This clears the stored configuration and restarts the device into setup mode. To wipe everything, including cached profiles, erase the flash and flash the firmware again:

```sh
idf.py -p /dev/ttyUSB0 erase-flash flash
```

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `Failed to mount SPIFFS` | The default partition table is in use | Configure the custom partition table ([step 2](#2-configure-the-project)), then rebuild and flash |
| `idf.py: command not found` | ESP-IDF is not loaded in this shell | Run `. "$IDF_PATH/export.sh"` |
| Setup returns `failed to connect to Wi-Fi` | Wrong credentials, or a 5 GHz-only network | Check the password and use a 2.4 GHz network |
| Setup returns `profile checksum mismatch` | The downloaded profile doesn't match the manifest's checksum | Update the manifest `checksum`, or remove it while developing |
| Setup returns `selected product was not found in manifest` | The product ID is not in the manifest | Check the `id` values in the manifest |
| `configured bus does not match profile protocol` | Example: a UART bus was configured for an SPI profile | Run setup again with the bus type the profile expects |
| The device keeps returning to setup mode | Saved Wi-Fi is unreachable for 30 seconds at boot | Make sure the network is in range, or run setup again |

## Hardware safety

Air-conditioner indoor units can expose mains voltage close to their control connectors. Before you connect anything:

- Turn off power to the unit at the breaker. Switching it off with the remote is not enough.
- Check the voltage levels of the control port. The ESP32-S3 GPIOs are **3.3 V only**. Use level shifting or isolation where needed.
- Confirm the pinout for your specific model. Profiles describe the protocol, not the wiring.
- Opening or modifying your unit may void its warranty. You are responsible for your own installation.

## Next steps

- [Architecture](architecture.md): how the firmware is structured.
- [AC Profiles](profiles.md): add support for another air conditioner.
- [HTTP API](http-api.md): control the device from scripts or home automation.
