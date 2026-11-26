# Security

## Reporting a vulnerability

Please do **not** open a public issue for security problems. Report them privately through [GitHub Security Advisories](https://github.com/simply-justin/open-comfort/security/advisories/new), with steps to reproduce and the firmware commit you tested.

## Development defaults to be aware of

- The setup network uses a WPA2 password that is the same on every device unless `CONFIG_OPENCOMFORT_PORTAL_AP_PASSWORD` is overridden per device.
- The runtime portal is plain HTTP on the local network. Keep devices on a trusted LAN or IoT network and never forward the port to the internet.
- Flash encryption and Secure Boot are off by default, so anyone with physical access can read the flash.
- Firmware updates (online and uploaded) must be signed with the release key, without hardware Secure Boot (`CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT`, RSA-3072). This stops a stolen portal session or a tampered download from installing other firmware. It does not stop someone with physical access: USB flashing is never checked. Enable Secure Boot for that. See [Firmware signing](docs/runtime-api.md#firmware-signing).

# Production security enablement

This repository keeps **development defaults safe**. Flash encryption and
Secure Boot are **not** enabled in `sdkconfig.defaults` because enabling them
incorrectly can permanently lock a device.

## What the firmware already prepares

- Wi-Fi credentials are stored via ESP-IDF Wi-Fi NVS storage (not SPIFFS JSON).
- User configuration uses dual-slot NVS commits.
- MQTT credentials and the salted administrator credential are held in a
  separate NVS namespace and are never returned by the runtime API.
- The post-setup portal uses expiring sessions (remembered ones end after 90 days),
  CSRF tokens, bounded requests, login throttling per client, a `Host` header check
  against DNS rebinding, and `X-Frame-Options: DENY`. Changing the administrator
  password requires the current one.
- Remotely downloaded AC profiles require:
  - HTTPS with certificate-bundle verification
  - SHA-256 integrity check
  - ECDSA P-256 signature verification with an embedded public key
- Bundled SPIFFS profiles are trusted as part of the signed firmware image.

## Recommended production enablement order

Follow Espressif's documented workflow:

1. Enable **Secure Boot v2**
2. Enable **Flash Encryption**
3. Enable **NVS Encryption** (flash-encryption-protected `nvs_keys` partition)

Reference:

- https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/security/security-features-enablement-workflows.html
- https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/storage/nvs_encryption.html

## Helper files in this repo

- `partitions-prod.csv` — includes an `nvs_keys` partition for NVS encryption
- `sdkconfig.prod.defaults` — production overlays (apply intentionally)
- `sdkconfig.matter.prod.defaults` — Matter credential-provider and production
  security overlay for 16 MB hardware

Example:

```bash
cp sdkconfig.prod.defaults sdkconfig.defaults.prod.local
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.prod.defaults" reconfigure
```

Only do this with manufacturing key management in place.

## Firmware signing key

The release workflow signs every image with the GitHub secret `OPENCOMFORT_SIGNING_KEY`
(an RSA-3072 PEM key) and refuses to publish without it. Generate a key once:

```bash
python -m espsecure generate_signing_key --version 2 --scheme rsa3072 firmware-signing-key.pem
gh secret set OPENCOMFORT_SIGNING_KEY < firmware-signing-key.pem
```

Keep a backup outside the repository. Devices only accept updates signed with the key
of the firmware they run, so with a lost or replaced key every device needs one USB flash.
Sign a local build to install it through the portal:

```bash
python -m espsecure sign_data --version 2 --keyfile firmware-signing-key.pem \
  --output build/OpenComfort.signed.bin build/OpenComfort.bin
```

## Profile signing

Generate / rotate an ECDSA P-256 key pair for profile distribution:

```bash
openssl ecparam -name prime256v1 -genkey -noout -out keys/profile_signing_private.pem
openssl ec -in keys/profile_signing_private.pem -pubout -out keys/profile_signing_public.pem
```

Embed the public key in `main/src/Profiles/ProfileCrypto.cpp`.

Sign a profile:

```bash
python3 scripts/sign-profile.py \
  --key keys/profile_signing_private.pem \
  --profile main/spiffs/profiles/mhi.json \
  --profile-id mitsubishi_heavy_industries \
  --version 1.0.0
```

Put `sha256`, `signature`, and `url` into the remote catalog entry. Never commit
private signing keys.

## Factory reset / re-provisioning

Switching the power off and on 5 times in a row (each within 10 s) also performs a factory reset. Anyone with access to the power supply can therefore reset the device and take it over through setup; set `CONFIG_OPENCOMFORT_POWER_CYCLE_RESET_COUNT=0` where that matters.

Hold the board factory-reset GPIO (rev-A default: GPIO0 / BOOT) for
`CONFIG_OPENCOMFORT_FACTORY_RESET_HOLD_MS` (default 5s) at boot or during runtime.

This clears user NVS config and Wi-Fi credentials, then restarts into the
captive portal. Connector secrets and Matter fabrics are also cleared.

## Matter credentials

Matter development passcodes and certificates are not product credentials.
Production units require unique commissioning data and a valid DAC/PAI/CD
chain injected into the `fctry` partition under controlled manufacturing key
management. Disable
`CONFIG_OPENCOMFORT_MATTER_DEVELOPMENT_CREDENTIALS`, enable the factory-data
provider, and retain auditable mappings between serial numbers and injected
identity. Never commit DAC private keys or generated factory partition images.
