# Production security enablement for OpenComfort

This repository keeps **development defaults safe**. Flash encryption and
Secure Boot are **not** enabled in `sdkconfig.defaults` because enabling them
incorrectly can permanently lock a device.

## What the firmware already prepares

- Wi-Fi credentials are stored via ESP-IDF Wi-Fi NVS storage (not SPIFFS JSON).
- User configuration uses dual-slot NVS commits.
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

Hold the board factory-reset GPIO (rev-A default: GPIO0 / BOOT) for
`CONFIG_OPENCOMFORT_FACTORY_RESET_HOLD_MS` (default 5s) at boot or during runtime.

This clears user NVS config and Wi-Fi credentials, then restarts into the
captive portal.
