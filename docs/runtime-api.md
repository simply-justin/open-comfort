# Runtime API and connector model

OpenComfort has one canonical runtime state. The local web, Home Assistant MQTT,
and Matter connectors submit bounded commands to `RuntimeController`; only the
application task applies commands to the AC bus. Connector callbacks never call
UART, SPI, or CAN directly.

## Product profiles

- `legacy_4mb` is the default. It supports the local web portal and Home
  Assistant MQTT and uses `partitions.csv`.
- `matter_16mb` adds Matter over Wi-Fi, BLE commissioning, IPv6, and two OTA
  slots. Build it with the Matter component explicitly enabled:

  ```bash
  idf.py -B build-matter \
    -D OPENCOMFORT_MATTER_PROFILE=ON \
    -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.matter.defaults" \
    build
  ```

The Matter profile pins `espressif/esp_matter` to 1.5.0. Do not copy its managed
components into the legacy build. The ESP32-S3 and controller ecosystems must
be tested on hardware before release.

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
- change the friendly name or administrator password;
- replace Wi-Fi credentials.

Connector or Wi-Fi configuration changes are committed atomically and applied
after a controlled reboot. Holding the configured factory-reset GPIO clears
OpenComfort settings, connector secrets, Wi-Fi, and Matter fabrics.

## HTTP contract

All runtime endpoints are under `/api/v1`.

- `POST /auth/login` creates an in-memory session. Mutating requests require the
  returned token in `X-OC-CSRF`.
- `GET /device` reports identity and build capabilities.
- `GET /state` reports desired/reported state, revision, and limits.
- `PUT /state` queues a state command and returns `202` with a request ID.
- `GET|PUT /connectors` reads redacted settings or stages replacements.
- `POST /matter/commissioning-window` opens commissioning when Matter is active.
- `PUT /wifi` replaces stored station credentials and schedules a reboot.

Passwords are never returned by an API. Request bodies are bounded to 2 KiB,
login failures are rate-limited, sessions expire after one hour, and runtime
logs must not contain credentials or session tokens.

## Home Assistant MQTT

The MQTT connector publishes retained climate discovery, desired and reported
state, availability with a Last Will, and stable MAC-derived identifiers. It
subscribes to mode, target-temperature, and fan command topics. TLS uses the
ESP-IDF certificate bundle. MQTT credentials are stored separately from the
versioned user configuration.

## Matter manufacturing

Development builds visibly warn when development commissioning credentials are
allowed. Production builds must apply `sdkconfig.matter.prod.defaults`, inject a
unique discriminator/passcode and DAC/PAI/CD into the `fctry` partition, and
manage Secure Boot and flash-encryption keys outside the repository.

The first Matter representation is a Thermostat endpoint for broad controller
compatibility. The Matter Room Air Conditioner device type should remain a
separately validated product option until Apple Home, Google Home, and Home
Assistant behavior is confirmed for the release versions being supported.
