# Security

## Reporting a vulnerability

Please do **not** open a public issue for security problems. Report them privately through [GitHub Security Advisories](https://github.com/simply-justin/open-comfort/security/advisories/new), with steps to reproduce and the firmware commit you tested.

## Development defaults to be aware of

- The setup network uses a WPA2 password that is the same on every device unless `CONFIG_OPENCOMFORT_PORTAL_AP_PASSWORD` is overridden per device.
- The runtime portal is plain HTTP on the local network. Keep devices on a trusted LAN or IoT network and never forward the port to the internet.
- Flash encryption and Secure Boot are off by default, so anyone with physical access can read the flash.
- Without Secure Boot, firmware updates are checked for integrity, chip and project name, but not signed. Anyone signed in to the portal can install firmware, and online updates trust whatever the configured GitHub repository publishes (downloads use HTTPS with certificate verification). Enable Secure Boot so only images signed with your key can boot.

# Production security enablement

This repository keeps **development defaults safe**. Flash encryption and
Secure Boot are **not** enabled in `sdkconfig.defaults` because enabling them
incorrectly can permanently lock a device.

## What the firmware already prepares

- Wi-Fi credentials are stored via ESP-IDF Wi-Fi NVS storage (not SPIFFS JSON).
- User configuration uses dual-slot NVS commits.
- MQTT credentials and the salted administrator credential are held in a
  separate NVS namespace and are never returned by the runtime API.
- The post-setup portal uses expiring in-memory sessions, CSRF tokens, bounded
  requests, and login throttling.
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
