# AC Profiles

A **profile** is a JSON file that tells OpenComfort how to talk to a specific air-conditioner model: which bus it uses, how to encode commands, and how to frame the bytes on the wire. A **manifest** lists the available profiles so the setup wizard can offer them.

The firmware does not ship every profile. During setup it downloads only the profile you select and caches it on the device.

## Profile repository

Profiles are published in a separate Git repository, which devices read over HTTPS. The `configs/` folder in this repository is the reference layout:

```text
configs/
├── manifest.json
└── profiles/
    └── mitsubishi_heavy_industries.json
```

To host your own, publish this folder as a GitHub repository. Then either change `RemoteConfigProvider::kDefaultManifestUrl` or pass `manifestUrl` during setup.

## Manifest format

```json
{
  "version": 1,
  "products": [
    {
      "id": "mitsubishi_heavy_industries",
      "name": "Mitsubishi Heavy Industries",
      "module": "ac",
      "bus": "spi",
      "profileUrl": "https://raw.githubusercontent.com/opencomfort/configs/main/profiles/mitsubishi_heavy_industries.json",
      "version": "1.0.0",
      "checksum": "sha256:3b4c…"
    }
  ]
}
```

| Field | Required | Description |
|---|---|---|
| `id` | Yes | Stable identifier. Also used as the cached file name, so use lowercase letters, digits, and underscores |
| `name` | No | Label in the setup wizard. Defaults to `id` |
| `module` | No | Module type. Currently `ac` |
| `bus` | No | Shown in the wizard: `spi`, `uart`, `rs485`, or `can` |
| `profileUrl` | Yes | `https://` URL, or a `/spiffs/…` path for bundled profiles |
| `version` | No | Profile version, saved in the device config |
| `checksum` | No | SHA-256 of the profile file, as bare hex or `sha256:<hex>`. If present, downloads that don't match are rejected |

The manifest may be up to 16 KB and each profile up to 64 KB.

To generate a checksum:

```sh
sha256sum configs/profiles/my_unit.json
```

## Profile format

A complete example lives at [`configs/profiles/mitsubishi_heavy_industries.json`](../configs/profiles/mitsubishi_heavy_industries.json).

```json
{
  "brand": "Mitsubishi Heavy Industries",
  "protocol": { "bus": "spi", "bit_order": "MSB_FIRST", "mode": "mode0" },
  "temperature": { "min": 16, "max": 30 },
  "frame": {
    "enabled": true,
    "header": ["FC", "01", "30", "01"],
    "payloadLength": 18,
    "checksum": "sum8"
  },
  "data": [
    { "name": "RETURN-AIR", "bytes": ["C0", "80"] }
  ],
  "commands": {
    "set_power":       [ { "field": "DB0", "binary_or": ["02", "$POWER"] } ],
    "set_mode":        [ { "field": "DB0", "binary_or": ["20", "$MODE"] } ],
    "set_temperature": [ { "field": "DB2", "binary_or": ["80", "$TEMP"] } ],
    "set_fan_speed":   [ { "field": "DB1", "binary_or": ["08", "$FAN"] } ]
  }
}
```

### Top-level fields

| Field | Required | Description |
|---|---|---|
| `brand` | Yes | Display name, used in logs |
| `protocol.bus` | Yes | `spi`, `uart`, `rs485`, or `can`. Must match the bus configured during setup |
| `protocol.mode` | No | SPI mode, `mode0` to `mode3`. Default `mode0` |
| `protocol.bit_order` | No | Informational. Not used by the firmware yet |
| `temperature.min` / `max` | No | Supported setpoint range in °C. Requests outside this range are rejected |
| `frame` | No | How the DB fields are wrapped before sending. See [Framing](#framing) |
| `data` | No | Telemetry queries. See [Telemetry](#telemetry) |
| `commands` | Yes | Named command recipes. See [Commands](#commands) |

All byte values are **two-character hex strings**, for example `"0A"` or `"FF"`.

### Commands

The firmware keeps an 18-byte buffer of fields named `DB0` to `DB17`. Each command is a list of steps, and each step changes one field:

| Step form | Effect |
|---|---|
| `{ "field": "DB3", "value": "<token>" }` | Replaces the field with the token's value |
| `{ "field": "DB0", "binary_or": ["<token>", …] }` | For each token: clears that token's bits in the field, then ORs its value in |

A **token** is either a hex literal (`"02"`) or a variable. The API currently invokes these commands:

| Command | Triggered by | Variable | Encoding |
|---|---|---|---|
| `set_power` | `powerOn` | `$POWER` | off `0x00`, on `0x01` |
| `set_mode` | `mode` | `$MODE` | auto/off `0x00`, dry `0x02`, cool `0x03`, heat `0x04`, fan `0x06` |
| `set_temperature` | `targetTemp` | `$TEMP` | `(°C − temperature.min) × 2`, clamped to the range |
| `set_fan_speed` | `fanSpeed` | `$FAN` | auto `0x00`, low `0x01`, medium `0x02`, high `0x03` |

`$TROOM` uses the same encoding as `$TEMP`.

Because `binary_or` clears a variable's bits before setting them, values can be switched off again (for example, power on → off). A literal token clears and sets only its own bits, so flag bits such as `"02"` stay set.

> Profiles may define other commands (for example `set_vanes_horizontal`). The firmware loads them, but nothing calls them yet, and variables such as `$VANES0` are not supported. Invoking an unsupported variable returns an error instead of sending a partial frame.

### Framing

After all command steps run, the firmware builds the bytes that go on the bus:

- **`frame.enabled: false`** (or no `frame`): all 18 DB bytes are sent unchanged.
- **`frame.enabled: true`**: it sends `header` + the first `payloadLength` DB bytes + an optional checksum.

| Field | Description |
|---|---|
| `header` | Hex bytes sent before the payload |
| `payloadLength` | Number of DB fields to include, 1 to 18 |
| `checksum` | `"sum8"` appends the low byte of the sum of all header and payload bytes |

### Telemetry

`data` entries are named byte sequences sent to request a value from the unit. Every ~5 seconds, the AC module sends the first entry named `RETURN-AIR` or `SET-TEMP` and reads back the reply. It interprets the third byte of the reply as `currentTemp = byte / 2 + temperature.min`.

Other `data` entries are loaded but not yet polled.

## Adding a new air conditioner

1. **Learn the protocol.** Find community documentation or capture traffic between the indoor unit and its wired remote. Record the bus type, electrical levels, and frame layout.
2. **Write the profile.** Copy the example profile and adapt `protocol`, `frame`, and the four core commands.
3. **Test offline.** Put the profile in `main/spiffs/profiles/<id>.json` and add a matching entry with `"profileUrl": "/spiffs/profiles/<id>.json"` to `main/spiffs/manifest.json`. Then flash and run setup.
4. **Check the bytes.** Watch the `Sent AC payload` log lines, and compare against a logic analyser capture if you can.
5. **Publish.** Add the profile and its manifest entry (with `checksum`) to the profile repository, and open a pull request.

When you contribute a profile, include the models you tested it on, the wiring and pinout, and any known limitations.
