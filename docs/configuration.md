# Configuration

OpenComfort separates what the **product** is (fixed at build time) from what the **user** chooses (set during setup and stored on the device).

## Build-time options

Set these with `idf.py menuconfig` under **OpenComfort Product**, or in an `sdkconfig.defaults` overlay.

| Option | Default | Description |
|---|---|---|
| `OPENCOMFORT_PRODUCT_ID` | `oc-ac-controller-v1` | Product identifier baked into the image |
| `OPENCOMFORT_MODULE_*` | `AC` | Functional module. Unused modules are not linked |
| `OPENCOMFORT_BOARD_*` | `REV_A` | Board revision, which fixes all pins |
| `OPENCOMFORT_PROFILE_*` | `LEGACY_4MB` | Capability profile: `LEGACY_4MB` or `MATTER_16MB` |
| `OPENCOMFORT_MATTER_DEVELOPMENT_CREDENTIALS` | `y` (Matter only) | Allows development commissioning data. Disable for production |
| `OPENCOMFORT_RUNTIME_HTTP_PORT` | `80` | Port of the runtime portal |
| `OPENCOMFORT_MQTT_RECONNECT_MAX_MS` | `60000` | Upper bound of the MQTT reconnect back-off |
| `OPENCOMFORT_PORTAL_AP_PASSWORD` | `opencomfort` | WPA2 password of the setup network. Override per device in production |
| `OPENCOMFORT_PROFILE_CATALOG_URL` | empty | Optional HTTPS URL of a remote profile catalog |
| `OPENCOMFORT_FACTORY_RESET_HOLD_MS` | `5000` | How long the reset button must be held |

### Build defaults

| File | Purpose |
|---|---|
| `sdkconfig.defaults` | Target, 4 MB flash, custom partition table, certificate bundle, size-optimised build |
| `sdkconfig.matter.defaults` | Overlay for the 16 MB Matter profile |
| `sdkconfig.prod.defaults` | Production security overlay |
| `sdkconfig.matter.prod.defaults` | Production overlay for Matter hardware |

`sdkconfig` itself is generated and not committed.

## What setup stores

The captive portal writes a versioned `UserConfig` (schema 2) to NVS with dual-slot commits, so a power cut during a save cannot corrupt it. Older schema 1 configurations are migrated at boot.

| Field | Description |
|---|---|
| `friendlyName` | Device name, up to 32 characters. Also the mDNS instance name |
| `ac.profileId`, `ac.profileVersion`, `ac.modelId` | Selected profile and model |
| `ac.bus` | Bus type taken from the profile |
| `connectors.localWebEnabled` | Runtime portal on or off |
| `connectors.mqttEnabled`, `mqttHost`, `mqttPort`, `mqttTls`, `mqttDiscoveryPrefix` | Home Assistant MQTT settings (default port 8883 with TLS, prefix `homeassistant`) |
| `connectors.matterEnabled` | Matter on or off (16 MB profile only) |

Secrets are stored separately:

| Secret | Where |
|---|---|
| Wi-Fi credentials | ESP-IDF Wi-Fi NVS storage |
| Administrator password | Salted, in the connector secret namespace |
| MQTT username and password | Connector secret namespace |
| Matter fabrics | `matter` NVS partition |

No API ever returns a password.

## Changing settings later

Sign in to the runtime portal to change the friendly name, administrator password, MQTT settings, Matter and Wi-Fi. Connector and Wi-Fi changes are committed atomically and applied after a controlled restart. See [Runtime API](runtime-api.md#setup-and-later-changes).

To start over, use a [factory reset](getting-started.md#factory-reset).
