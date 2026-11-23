# Security

OpenComfort is early-stage firmware for personal use on a trusted home network. Read this page before you deploy it.

## Known limitations

| Area | Current behaviour | Recommendation |
|---|---|---|
| Setup access point | `OpenComfort-Setup` is an **open** network | Run setup somewhere private, and finish it promptly |
| HTTP API | No authentication and no TLS | Keep the device on a trusted LAN or IoT VLAN. Never forward its port to the internet |
| Reset endpoint | `POST /api/setup/reset` wipes config without confirmation | Same as above |
| Credentials | The Wi-Fi password is stored in plain text in `/spiffs/config.json` | Anyone with physical access to the device can read it. Use a dedicated IoT network |
| Profile downloads | HTTPS with the ESP-IDF certificate bundle. SHA-256 is verified when the manifest provides a `checksum` | Always publish checksums in your manifest |
| Flash | No flash encryption or secure boot configured | Enable them in `menuconfig` if your threat model needs it |

## Reporting a vulnerability

Please **do not** open a public issue for security problems. Report them privately through [GitHub Security Advisories](https://github.com/simply-justin/open-comfort/security/advisories/new). Include steps to reproduce and the firmware commit you tested.
