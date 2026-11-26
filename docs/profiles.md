# AC Profiles

A **profile** is a JSON file that tells OpenComfort how to talk to an air-conditioner family: which bus it uses, how big a frame is, how commands are encoded and which registers to poll. A **catalog** lists the profiles and the models they cover, so setup can offer a make and model list.

## Where profiles come from

Profiles are not part of the firmware. They live in [`profiles/`](../profiles) in this repository. On every push to `main` that touches that folder, the **Profiles** workflow (`.github/workflows/profiles.yml`) signs each profile and publishes the catalog on GitHub Pages:

```text
https://simply-justin.github.io/open-comfort/profiles/index.json
```

That URL is `CONFIG_OPENCOMFORT_PROFILE_CATALOG_URL`. A device downloads the catalog during setup (right after it joins Wi-Fi) and again at every boot. It downloads the profile it uses, checks its SHA-256 and ECDSA P-256 signature, and keeps it in SPIFFS, so it keeps working without internet. Each load from SPIFFS repeats the check. When the catalog lists a new version, the device fetches it at the next boot.

So adding or fixing a profile needs no firmware build: merge it, wait for the workflow, and restart the device.

The catalog file itself is not signed, so a device ignores `path` in it: an entry can only point to a signed download. Devices flashed over USB before this change still have the profiles that were bundled in SPIFFS; a downloaded profile with the same id takes precedence, and the old copy is only used when nothing can be downloaded.

## Catalog format

`profiles/index.json` is the source list:

```json
{
  "schemaVersion": 1,
  "profiles": [
    {
      "id": "mitsubishi_heavy_industries",
      "make": "mitsubishi-heavy-industries",
      "makeName": "Mitsubishi Heavy Industries",
      "models": ["SRK25ZSX", "SRK35ZSX", "Generic"],
      "bus": "spi",
      "profileVersion": "1.1.0",
      "file": "mhi.json"
    }
  ]
}
```

| Field | Description |
|---|---|
| `id` | Stable profile identifier, stored in the user configuration. At most 48 bytes, also together with `\|` and each model |
| `make` / `makeName` | Make identifier and the label shown in setup |
| `models` | Models offered in setup, at most 16 |
| `bus` | `spi`, `uart` or `can`. Must be allowed by the product |
| `profileVersion` | Version string, part of the signed message. Raise it with every change |
| `file` | The profile's file name in `profiles/` |

The workflow replaces `file` with `url`, `sha256` and `signature` in the published catalog. `scripts/build-profile-catalog.py` checks the limits above and refuses a catalog over 48 KiB or with more than 32 entries.

## Profile format

The reference profile is [`profiles/mhi.json`](../profiles/mhi.json).

| Field | Description |
|---|---|
| `schemaVersion` | Profile schema version, currently `1` |
| `profileId`, `profileVersion` | Must match the catalog entry |
| `brand`, `models` | Descriptive metadata |
| `protocol.bus` | `spi`, `uart` or `can` |
| `protocol.mode` | SPI mode, `mode0` to `mode3` |
| `protocol.frequencyHz` | SPI clock. Capped by the board (1 MHz on rev-A) |
| `protocol.baudRate` | UART baud rate. Capped by the board (115200 on rev-A) |
| `frameSize` | Bytes per command frame, at most 32 |
| `capabilities.minTemp` / `maxTemp` | Setpoint range in °C |
| `capabilities.modes` | Supported modes |
| `data` | Registers to poll: `{ "name": "RETURN-AIR", "bytes": ["C0", "80"] }` |
| `commands` | Named recipes that fill frame fields (below) |

Byte values are two-character hex strings.

### Commands

The engine runs `set_power`, `set_mode`, `set_temperature` and `set_fan_speed` for every desired-state change. Each is a list of steps on frame fields `DB0` to `DB{frameSize-1}`:

```json
"set_mode": [ { "field": "DB0", "binary_or": ["20", "$MODE"] } ]
```

| Step | Effect |
|---|---|
| `{ "field": "DB3", "value": "<token>" }` | Sets the field to the token's value |
| `{ "field": "DB0", "binary_or": ["<token>", …] }` | ORs every token into the field |

A token is a hex literal or one of these variables:

| Variable | Encoding |
|---|---|
| `$POWER` | off `0x00`, on `0x01` |
| `$MODE` | off `0x00`, cool `0x01`, heat `0x02`, dry `0x03`, fan `0x04`, auto `0x05` |
| `$TEMP` | Setpoint in whole °C, rounded and clamped to `minTemp`…`maxTemp` |
| `$FAN` | auto `0x00`, low `0x01`, medium `0x02`, high `0x03` |

### Polling

Every 2 seconds the module sends each `data` request and reads the reply. `RETURN-AIR` and `SET-TEMP` are mapped to the reported room and set temperature (first reply byte, in °C). Other registers are polled but not yet reported.

## Signing

The workflow signs with the `OPENCOMFORT_PROFILE_SIGNING_KEY` secret (an ECDSA P-256 private key). The signed message is `<id>|<sha256>|<profileVersion>`. The matching public key is embedded in `main/src/Profiles/ProfileCrypto.cpp`. To check a catalog locally:

```sh
python3 scripts/build-profile-catalog.py --key profile-signing-key.pem \
  --base-url https://simply-justin.github.io/open-comfort/profiles --out site/profiles
```

Key handling is described in [SECURITY.md](../SECURITY.md#profile-signing). Never commit private keys.

## Adding an air conditioner

1. **Learn the protocol.** Use community documentation or capture traffic between the unit and its wired remote. Note the bus, levels and frame layout.
2. **Write the profile.** Start from `mhi.json` and adapt `protocol`, `frameSize`, `capabilities`, `data` and the four commands.
3. **List it.** Add the file under `profiles/` and an entry to `profiles/index.json`. After the merge the Profiles workflow publishes it; no firmware build is needed.
4. **Test on hardware.** Run setup, select the model and compare the frames on a logic analyser with the original remote.
5. **Contribute.** Open a pull request with the models you tested, the connector and any limitations.

The Mitsubishi Heavy Industries profile uses the dedicated 20-byte CNS
codec. The indoor unit supplies SCK and MOSI. On rev-A these are inputs GPIO12
and GPIO11, and GPIO13 supplies MISO. No external CS is used. The peripheral
uses mode 3, least-significant bit first, and synchronizes on gaps in SCK.
A valid signature and checksum are required before sending settings. A completed
SPI transfer reports transmission, while actual operation requires testing on
the connected indoor unit. The default rev-A configuration uses 20-byte frames.

The XIAO ESP32-S3 attic controller uses SCK GPIO7, MOSI GPIO9 and MISO GPIO8
with 33-byte frames and a second checksum. One firmware serves both boards: at
boot it counts clock edges on GPIO12 and GPIO7 for about 200 ms each, with
pull-downs so an unwired pin stays quiet, and uses the wiring whose SCK the
indoor unit clocks. The choice is kept in NVS (`oc_boot/mhi_wiring`, not erased by
a factory reset) for boots where the unit is not clocking yet. The Status page
reports the SCK pin in use and the clock bursts seen since boot when the AC does
not answer.
