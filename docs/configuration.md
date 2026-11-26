# Configuration

OpenComfort separates what the **product** is (fixed at build time) from what the **user** chooses (set during setup and stored on the device).

## Build-time options

Set these with `idf.py menuconfig` under **OpenComfort Product**, or in an `sdkconfig.defaults` overlay.

| Option | Default | Description |
|---|---|---|
| `OPENCOMFORT_PRODUCT_ID` | `oc-ac-controller-v1` | Product identifier baked into the image |
| `OPENCOMFORT_MODULE_*` | `AC` | Functional module. Unused modules are not linked |
| `OPENCOMFORT_BOARD_*` | `REV_A` | Board revision, which fixes all pins |
| `OPENCOMFORT_PROFILE_*` | `STANDARD` | Capability profile: `STANDARD` or `FULL` |
| `OPENCOMFORT_MATTER_DEVELOPMENT_CREDENTIALS` | `y` (Matter only) | Allows development commissioning data. Disable for production |
| `OPENCOMFORT_RUNTIME_HTTP_PORT` | `80` | Port of the runtime portal |
| `OPENCOMFORT_MQTT_RECONNECT_MAX_MS` | `60000` | Upper bound of the MQTT reconnect back-off |
| `OPENCOMFORT_PORTAL_AP_PASSWORD` | `opencomfort` | WPA2 password of the setup network. Override per device in production |
| `OPENCOMFORT_PROFILE_CATALOG_URL` | GitHub Pages catalog of this repository | HTTPS URL of the signed profile catalog. Empty disables downloads; see [AC Profiles](profiles.md) |
| `OPENCOMFORT_UPDATE_REPO` | `simply-justin/open-comfort` | GitHub repository whose releases provide online updates; empty turns them off |
| `OPENCOMFORT_FACTORY_RESET_HOLD_MS` | `5000` | How long the reset button must be held |
| `OPENCOMFORT_POWER_CYCLE_RESET_COUNT` | `5` | Quick power cycles that trigger a factory reset; 0 turns it off |
| `OPENCOMFORT_POWER_CYCLE_WINDOW_MS` | `10000` | A power-on counts only if the power is cut again within this time |

### Build defaults

| File | Purpose |
|---|---|
| `sdkconfig.defaults` | Target, 4 MB flash, OTA partition table with app rollback, signed updates, certificate bundle, size-optimised build |
| `sdkconfig.matter.defaults` | Matter profile and 16 MB layout; apply after `sdkconfig.defaults` |
| `sdkconfig.full.defaults` | Adds Home Assistant MQTT to the Matter profile; apply after `sdkconfig.matter.defaults` |
| `sdkconfig.8mb.defaults` | Selects the shared Matter/Full 8 MB layout; apply last |
| `sdkconfig.prod.defaults` | Production security overlay |
| `sdkconfig.matter.prod.defaults` | Production overlay for Matter hardware |

`sdkconfig` itself is generated and not committed.

## What setup stores

The captive portal writes a versioned `UserConfig` (schema 3) to NVS with dual-slot commits, so a power cut during a save cannot corrupt it. Older schema 1 and 2 configurations are migrated at boot.

| Field | Description |
|---|---|
| `friendlyName` | Device name, up to 32 characters. Also the mDNS instance name |
| `hostname` | Local network name, up to 32 characters. Set it under Settings → This device → Local hostname. An empty value restores the MAC-derived name. |
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
