# Configuration

OpenComfort stores its configuration as JSON in SPIFFS at `/spiffs/config.json`. In normal use the setup wizard writes this file for you, so you rarely edit it by hand. This page explains what the file contains and how it is created.

## First-time setup

The device enters **setup mode** when:

- `/spiffs/config.json` is missing, cannot be parsed, or fails validation, or
- the saved Wi-Fi network cannot be reached within 30 seconds of boot.

In setup mode, the device:

1. Starts an **open** access point named `OpenComfort-Setup`, with the setup wizard at `http://192.168.4.1`.
2. Lists products from the manifest (`GET /api/setup/products`).
3. Receives the setup request (`POST /api/setup`) and:
   1. Joins the given Wi-Fi network (20-second timeout).
   2. Loads the manifest and finds the selected product.
   3. Downloads the product's profile (up to 64 KB) and verifies its checksum if the manifest provides one.
   4. Checks that the profile has `brand`, `protocol`, and `commands`.
   5. Caches the profile at `/spiffs/profiles/<productId>.json`.
   6. Validates and saves `/spiffs/config.json`.
4. Continues to normal boot.

If any step fails, the setup request returns an error and the device stays in setup mode, so you can correct the input and try again.

### Fallbacks for offline development

| Fetch | Fallback when it fails |
|---|---|
| Remote manifest | Bundled `/spiffs/manifest.json` |
| Remote profile | Bundled `/spiffs/profiles/<productId>.json` (no checksum check) |

The bundled files come from `main/spiffs/` and are flashed along with the firmware. With these fallbacks you can develop without network access or a published profile repository.

### Setup request

`POST /api/setup` takes a JSON body of up to 2 KB:

| Field | Required | Default | Description |
|---|---|---|---|
| `wifiSsid` | Yes | | Network to join |
| `wifiPassword` | No | `""` | Network password |
| `productId` | Yes | | Product `id` from the manifest |
| `friendlyName` | No | `OpenComfort Device` | Device name shown in `/api/status` |
| `acFriendlyName` | No | `Air Conditioner` | Name of the AC module |
| `manifestUrl` | No | Built-in default | Use a different manifest (for example a fork) |
| `bus` | No | SPI with no pins set | Bus settings. See [Bus settings](#bus-settings) |

The built-in default manifest URL is:

```text
https://raw.githubusercontent.com/opencomfort/configs/main/manifest.json
```

Example:

```json
{
  "wifiSsid": "MyNetwork",
  "wifiPassword": "secret",
  "productId": "mitsubishi_heavy_industries",
  "friendlyName": "Living room",
  "bus": {
    "type": "spi",
    "sckPin": 12,
    "mosiPin": 11,
    "misoPin": 13,
    "csPin": 10,
    "frequency": 1000000
  }
}
```

## Bus settings

The `bus` object chooses the transport and its pins. Leave out any field to use its default. Pins default to `-1` (not connected), so set every pin your wiring uses.

**SPI** (`"type": "spi"`)

| Field | Default | Description |
|---|---|---|
| `host` | `2` | SPI host peripheral |
| `sckPin`, `mosiPin`, `misoPin`, `csPin` | `-1` | GPIO numbers |
| `mode` | `0` | Overridden by the profile's `protocol.mode` |
| `frequency` | `1000000` | Clock in Hz |

**UART / RS485** (`"type": "uart"` or `"rs485"`)

| Field | Default | Description |
|---|---|---|
| `port` | `1` | UART port number |
| `txPin`, `rxPin` | `-1` | GPIO numbers |
| `baudRate` | `9600` | Baud rate |

**CAN** (`"type": "can"`)

| Field | Default | Description |
|---|---|---|
| `txPin`, `rxPin` | `-1` | GPIO numbers |
| `baudRate` | `500000` | Bit rate |
| `id` | `256` (`0x100`) | Frame identifier |

The bus type must match the profile's `protocol.bus`. If they differ, the AC module refuses to boot.

## `config.json` reference

```json
{
  "configVersion": 1,
  "isValid": true,
  "friendlyName": "Living room",
  "selectedProductId": "mitsubishi_heavy_industries",
  "wifi": {
    "ssid": "MyNetwork",
    "password": "secret"
  },
  "module": {
    "type": "ac",
    "productId": "mitsubishi_heavy_industries",
    "acProfilePath": "/spiffs/profiles/mitsubishi_heavy_industries.json",
    "acFriendlyName": "Air Conditioner",
    "bus": { "type": "spi", "host": 2, "sckPin": 12, "mosiPin": 11, "misoPin": 13, "csPin": 10, "mode": 0, "frequency": 1000000 },
    "remote": {
      "manifestUrl": "",
      "profileUrl": "https://raw.githubusercontent.com/opencomfort/configs/main/profiles/mitsubishi_heavy_industries.json",
      "profileVersion": "1.0.0",
      "checksum": ""
    }
  }
}
```

| Field | Description |
|---|---|
| `configVersion` | Schema version. Must be positive |
| `isValid` | Written as `true` after a successful setup. The device ignores a config that has this set to `false` |
| `friendlyName` | Required. Device display name |
| `selectedProductId` | Product chosen during setup |
| `wifi.ssid` / `wifi.password` | Station credentials. `ssid` is required |
| `module.type` | Module kind. Only `ac` is supported |
| `module.productId` | Required for `ac` |
| `module.acProfilePath` | Required for `ac`. The cached profile to load at boot |
| `module.bus` | See [Bus settings](#bus-settings) |
| `module.remote` | Where the profile came from. Kept for reference only and not used at boot |

> **Note:** the Wi-Fi password is stored in plain text in flash. See [SECURITY.md](../SECURITY.md).

## Bundled assets

Everything under `main/spiffs/` is packed into the `storage` partition at build time:

```text
main/spiffs/
├── manifest.json                       # Fallback manifest (profileUrl points into /spiffs)
└── profiles/
    └── mitsubishi_heavy_industries.json  # Example and fallback profile
```

SPIFFS has a flat namespace and limited space (576 KB). Keep bundled assets small.
