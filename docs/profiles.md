# AC Profiles

A **profile** is a JSON file that tells OpenComfort how to talk to an air-conditioner family: which bus it uses, how big a frame is, how commands are encoded and which registers to poll. A **catalog** lists the profiles and the models they cover, so setup can offer a make and model list.

## Bundled and remote catalogs

The bundled catalog is `main/spiffs/profiles/index.json`. It ships in the SPIFFS image and is trusted as part of the firmware. Portal (OTA) updates replace only the app, so changes to bundled profiles reach a device through a USB flash or the remote catalog.

When `CONFIG_OPENCOMFORT_PROFILE_CATALOG_URL` is set, the device also downloads a remote catalog over HTTPS after it joins Wi-Fi. Remote entries are merged with the bundled ones. A remote profile is only used after its SHA-256 and ECDSA signature check out.

## Catalog format

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
      "profileVersion": "1.0.0",
      "path": "profiles/mhi.json",
      "sha256": "37360adf…",
      "bundled": true
    }
  ]
}
```

| Field | Description |
|---|---|
| `id` | Stable profile identifier, stored in the user configuration |
| `make` / `makeName` | Make identifier and the label shown in setup |
| `models` | Models offered in setup, at most 16 |
| `bus` | `spi`, `uart` or `can`. Must be allowed by the product |
| `profileVersion` | Version string, also part of the signed message |
| `path` | Location of a bundled profile under `/spiffs` |
| `url` | HTTPS location of a remote profile |
| `sha256` | SHA-256 of the profile file, lowercase hex |
| `signature` | ECDSA P-256 signature, hex. Required for remote profiles |
| `bundled` | `true` for profiles shipped in SPIFFS |

A catalog holds at most 32 entries. Downloads are limited to 48 KiB.

## Profile format

The reference profile is [`main/spiffs/profiles/mhi.json`](../main/spiffs/profiles/mhi.json).

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

## Signing a remote profile

```sh
python3 scripts/sign-profile.py \
  --key keys/profile_signing_private.pem \
  --profile path/to/profile.json \
  --profile-id my_profile \
  --version 1.0.0
```

The script prints `sha256` and `signature`. The signed message is `<id>|<sha256>|<profileVersion>`. Put both values and the profile's HTTPS `url` in the remote catalog entry. The matching public key is embedded in `main/src/Profiles/ProfileCrypto.cpp`. Key handling is described in [SECURITY.md](../SECURITY.md#profile-signing). Never commit private keys.

## Adding an air conditioner

1. **Learn the protocol.** Use community documentation or capture traffic between the unit and its wired remote. Note the bus, levels and frame layout.
2. **Write the profile.** Start from `mhi.json` and adapt `protocol`, `frameSize`, `capabilities`, `data` and the four commands.
3. **Bundle it.** Add the file under `main/spiffs/profiles/` and an entry with its `sha256` to `index.json`.
4. **Test on hardware.** Run setup, select the model and compare the frames on a logic analyser with the original remote.
5. **Contribute.** Open a pull request with the models you tested, the connector and any limitations.

The bundled Mitsubishi Heavy Industries profile uses the dedicated 20-byte CNS
codec. The indoor unit supplies SCK and MOSI. On rev-A these are inputs GPIO12
and GPIO11, and GPIO13 supplies MISO. No external CS is used. The peripheral
uses mode 3, least-significant bit first, and synchronizes on gaps in SCK.
A valid signature and checksum are required before sending settings. A completed
SPI transfer reports transmission, while actual operation requires testing on
the connected indoor unit. The default rev-A configuration uses 20-byte frames.

The XIAO ESP32-S3 attic controller uses 33-byte frames with a second checksum.
Build with `-D OPENCOMFORT_MHI_BOARD=xiao-s3-attic` for SCK GPIO7, MOSI GPIO9
and MISO GPIO8. CMake remembers the board in that build directory. Use
`-D OPENCOMFORT_MHI_BOARD=rev-a` to restore the rev-A settings. The Status page
reports clock bursts seen since boot when the AC does not answer.
The Full 8 MB attic build looks for `opencomfort-full-8mb-xiao-attic.bin` during
online updates. Until a matching release asset is published, install a locally
built attic image through USB or the portal.
