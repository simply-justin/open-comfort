# Runtime API and connector model

OpenComfort has one canonical runtime state. The local web, Home Assistant MQTT,
and Matter connectors submit bounded commands to `RuntimeController`; only the
application task applies commands to the AC bus. Connector callbacks never call
UART, SPI, or CAN directly.

## Product profiles

- `ha-4mb` is the default. It supports the local web portal and Home
  Assistant MQTT and uses `partitions.csv`.
- `matter-8mb` and `matter-16mb` support the local web portal and Matter,
  without Home Assistant MQTT. For an 8 MB board:

  ```bash
  idf.py -B build-matter8 \
    -D OPENCOMFORT_MATTER=ON \
    -D SDKCONFIG=build-matter8/sdkconfig \
    -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.matter.defaults;sdkconfig.8mb.defaults" \
    build
  ```

- `full-8mb` and `full-16mb` include both Home Assistant MQTT and Matter;
  choose one integration in the portal. They include Matter over Wi-Fi, BLE
  commissioning, IPv6, and two OTA slots. Build the 16 MB variant with:

  ```bash
  idf.py -B build-full \
    -D OPENCOMFORT_MATTER=ON \
    -D SDKCONFIG=build-full/sdkconfig \
    -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.matter.defaults;sdkconfig.full.defaults" \
    build
  ```

  Boards with 8 MB flash add `sdkconfig.8mb.defaults` as the last overlay. It
  selects `partitions-matter-8mb.csv`, with two 2.94 MB OTA slots:

  ```bash
  idf.py -B build-full8 \
    -D OPENCOMFORT_MATTER=ON \
    -D SDKCONFIG=build-full8/sdkconfig \
    -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.matter.defaults;sdkconfig.full.defaults;sdkconfig.8mb.defaults" \
    build
  ```

The Matter profile pins `espressif/esp_matter` to 1.5.0. Do not copy its managed
components into the standard build. The ESP32-S3 and controller ecosystems must
be tested on hardware before release.

### Reusing a build directory after the profile rename

`SDKCONFIG_DEFAULTS` only supplies defaults; it does not replace values already
saved in a build directory's `sdkconfig`. CMake also remembers the defaults file
list and options from the previous build.

If a build reports missing `partitions-full-8mb.csv` or
`sdkconfig.matter8.defaults`, back up its generated configuration, then rerun
the complete build command above for that directory:

```bash
# Run only for the directory you are migrating.
mv build-full8/sdkconfig build-full8/sdkconfig.before-profile-rename
# Or, for the old Matter build:
mv build-matter8/sdkconfig build-matter8/sdkconfig.before-profile-rename
```

Reapply any deliberate `menuconfig` customizations after regeneration. The current
option is `OPENCOMFORT_MATTER=ON` (formerly `OPENCOMFORT_MATTER_PROFILE`), and
the 8 MB overlay is `sdkconfig.8mb.defaults`. Both Matter and Full use
`partitions-matter-8mb.csv`; a separate Full partition table is not needed.
This only regenerates host build configuration and does not erase device settings.

After a successful Full build, flash from that same directory, replacing `PORT`
with your selected serial device:

```bash
idf.py -B build-full8 -p PORT flash monitor
```

Use `-B build-matter8` instead only when you want the Matter-only firmware.

## Setup and later changes

Initial captive setup collects Wi-Fi, AC profile, an administrator password,
and optional MQTT/Matter settings. The local portal remains enabled as the
fallback control surface.

After setup, browse to the `_http._tcp` service advertised by the device
(`opencomfort-XXXX.local` on networks that support mDNS). Sign in to:

- control power, mode, setpoint, and fan speed;
- replace MQTT broker settings and credentials;
- enable or disable Matter on supported hardware;
- open a Matter commissioning window;
- change the friendly name, local hostname or administrator password;
- replace Wi-Fi credentials.

Connector or Wi-Fi configuration changes are committed atomically and applied
after a controlled reboot. Holding the configured factory-reset GPIO clears
OpenComfort settings, connector secrets, Wi-Fi, and Matter fabrics.

## HTTP contract

All runtime endpoints are under `/api/v1`.

- `POST /auth/login` with `{"password": "...", "remember": true|false}` creates a session
  (up to six at once) and sets the `HttpOnly` session cookie. A normal session lasts one hour
  and ends at a restart. With `remember` it is stored in NVS (`oc_secrets`), the cookie lasts a
  year, and it ends at sign-out, a password change, a factory reset or 90 days after sign-in
  (counted by the SNTP clock). When all slots are
  taken, the short session that expires first is replaced. Mutating requests require the
  returned `csrfToken`, which differs from the cookie, in `X-OC-CSRF`.
- `GET /device` reports identity, `firmwareVersion` and build capabilities, including `ota`.
- `GET /state` reports desired/reported state, revision, and limits.
- `PUT /state` queues a state command and returns `202` with a request ID.
- `GET|PUT /connectors` reads redacted settings or stages replacements. A new
  `adminPassword` needs the current one in `currentPassword` (`400 wrong_current_password`
  otherwise). A PUT with
  `{"hostname":"airco-woonkamer"}` selects `airco-woonkamer.local` after restart.
  An empty string restores the default. `GET /device` returns the resolved DNS label
  in `hostname` and the saved override in `hostnameOverride`.
- `POST /matter/commissioning-window` opens commissioning when Matter is active.
- `PUT /wifi` replaces stored station credentials and schedules a reboot.
- `GET /status` reports Wi-Fi (SSID, RSSI, IP, mDNS name), connector states, whether the
  unit answers, the last command and its source, uptime, free heap and firmware version.
- `GET /history` returns the room temperature once a minute for the last 24 hours
  (`null` where the unit gave no reading). It is kept in RAM and starts empty after a restart.
- `GET|PUT /schedule` reads or replaces the time zone (POSIX TZ string) and up to eight
  rules (`time`, `days` bit mask Monday = bit 0, `action` `on`/`off`, `mode`, `targetTempC`).
  Rules run on the device once SNTP has set the clock. `POST /schedule/sleep` with
  `{"minutes": n}` turns the unit off after `n` minutes (0 cancels).
- `GET /matter/pairing` returns the Matter QR payload, the manual code, the QR modules
  and the seconds left in the commissioning window (Matter builds only).
- `GET|PUT /update` reads the online update state (`state`, `current`, `latest`, `progress`,
  `channel` `release`/`prerelease`, `autoInstall`) or changes `channel` and `autoInstall`.
  `POST /update/check` starts a check against GitHub releases; `POST /update/install`
  downloads the available version over HTTPS into the inactive slot and restarts.
- `POST /firmware` takes a raw `OpenComfort.bin` body, writes it to the inactive OTA
  slot and schedules a reboot. Images from another project or chip are rejected, and so
  are images not signed with the release key (see "Firmware signing" below).

Passwords are never returned by an API. Request bodies are bounded to 2 KiB,
login failures are slowed per client address (1 s after a failure, 30 s after five;
`429 too_many_attempts`), sessions expire after one hour, and runtime logs must not
contain credentials or session tokens.

API requests must carry a `Host` header that is the device's IP address, its hostname or
the hostname with a suffix (`airco.local`, `airco.fritz.box`); anything else gets
`400 host_not_allowed`. This blocks DNS rebinding from websites. Pages are served with
`X-Frame-Options: DENY`, so other sites cannot frame the portal.

The portal is plain HTTP: on the local network the password and session cookie are not
encrypted. Use it on a network you trust, or reach it through a VPN.

## Firmware signing

Release images are signed (Secure Boot V2 RSA-3072 signature block, without hardware
Secure Boot: `CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT`). A running OpenComfort only accepts
an update, online or uploaded, that is signed with the same key as itself. The release
workflow signs every image with the `OPENCOMFORT_SIGNING_KEY` secret and refuses to publish
without it.

Local `idf.py build` images are not signed. Flash them over USB. A device running an
unsigned build cannot install updates; flash a signed release over USB to restore that.
Firmware up to v0.2.0-beta.4 does not check signatures and installs signed images normally.
Losing the key means devices can only be updated over USB.

## Home Assistant MQTT

Turn on Home Assistant in setup or Settings and point it at your broker. The defaults
(`homeassistant.local`, port 1883, no TLS) match Home Assistant's Mosquitto add-on; add
a username and password when the broker asks for one. The device then announces itself
through MQTT discovery (retained, under the `homeassistant` prefix) and appears as one
device with these entities:

| Entity | Type | Notes |
|---|---|---|
| *Device name* | climate | Power, mode (`off`, `auto`, `cool`, `heat`, `dry`, `fan_only`), setpoint in whole degrees, fan, room temperature |
| Room temperature | sensor | For history graphs and statistics |
| Sleep timer | select | Off, 30 min, 1 h, 2 h, 4 h |
| Unit answering | binary_sensor (diagnostic) | Whether the air conditioner replies on the bus |
| Wi-Fi signal | sensor (diagnostic, off by default) | dBm |
| Firmware | update | Installed and newest release; install from Home Assistant |

It re-announces when Home Assistant restarts (`homeassistant/status` = `online`) and
reports `offline` through a Last Will. Topics live under `opencomfort/<id>/`; commands
end in `/set`. TLS, when turned on, uses the ESP-IDF certificate bundle. MQTT
credentials are stored separately from the versioned user configuration.

## Matter manufacturing

Development builds visibly warn when development commissioning credentials are
allowed. Production builds must apply `sdkconfig.matter.prod.defaults`, inject a
unique discriminator/passcode and DAC/PAI/CD into the `fctry` partition, and
manage Secure Boot and flash-encryption keys outside the repository.

Operational Matter fabrics and counters use the separate `matter` NVS
partition. Factory reset erases that partition while preserving manufacturing
identity in `fctry`. Networking is supplied by OpenComfort captive provisioning
through Matter's custom-network condition; the Matter stack does not take
ownership of station credentials.

The first Matter representation is a Thermostat endpoint for broad controller
compatibility. The Matter Room Air Conditioner device type should remain a
separately validated product option until Apple Home, Google Home, and Home
Assistant behavior is confirmed for the release versions being supported.
