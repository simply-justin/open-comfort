# HTTP API

OpenComfort runs a small HTTP server on port 80. Which routes are available depends on the mode the device is in.

- All JSON responses use `Content-Type: application/json`.
- Errors are returned as `{"error": "<message>"}`, usually with status `400` (or `404` / `500`).
- There is **no authentication**. Only expose the device on networks you trust. See [SECURITY.md](../SECURITY.md).

## Normal mode

The base URL is `http://<device-ip>`.

| Method | Path | Description |
|---|---|---|
| `GET` | `/` | Browser control UI |
| `GET` | `/api/status` | Device name and status |
| `GET` | `/api/modules` | List of active modules |
| `GET` | `/api/modules/ac` | AC module info |
| `GET` | `/api/modules/ac/state` | Current AC state |
| `POST` | `/api/modules/ac/command` | Change AC settings |
| `POST` | `/api/setup/reset` | Clear config and restart into setup mode |

### `GET /api/status`

```json
{ "device": "Living room", "configured": true }
```

### `GET /api/modules`

```json
{ "modules": ["ac"] }
```

### `GET /api/modules/ac`

```json
{ "type": "ac", "profile": "/spiffs/profiles/mitsubishi_heavy_industries.json" }
```

### `GET /api/modules/ac/state`

```json
{
  "powerOn": true,
  "mode": "cool",
  "currentTemp": 23.5,
  "targetTemp": 21.0,
  "fanSpeed": "auto"
}
```

`targetTemp`, `mode`, `fanSpeed`, and `powerOn` reflect the last command sent. They are not read back from the unit. `currentTemp` comes from telemetry polling.

### `POST /api/modules/ac/command`

Send one or more fields. Fields you leave out stay unchanged. The body may be up to 1 KB.

| Field | Type | Values |
|---|---|---|
| `powerOn` | boolean | `true`, `false` |
| `mode` | string | `off`, `cool`, `heat`, `dry`, `fan`, `auto` |
| `targetTemp` | number | °C, within the profile's `temperature.min` to `max` |
| `fanSpeed` | string | `auto`, `low`, `medium` (or `med`), `high` |

```sh
curl -X POST http://<device-ip>/api/modules/ac/command \
  -H 'Content-Type: application/json' \
  -d '{"powerOn": true, "mode": "cool", "targetTemp": 21, "fanSpeed": "auto"}'
```

Success:

```json
{ "ok": true }
```

Common errors:

| Error | Cause |
|---|---|
| `invalid JSON` | The body is not valid JSON |
| `unsupported mode` / `unsupported fanSpeed` | The value is not in the table above |
| `target temperature out of supported range` | Outside the profile's range |
| `no supported command fields provided` | None of the fields above were present |
| `profile command not found: <name>` | The profile has no recipe for this command |
| `bus write failed` | Hardware or driver error. Check wiring |

### `POST /api/setup/reset`

```json
{ "ok": true, "restarting": true }
```

Clears `/spiffs/config.json` and restarts the device immediately. Cached profiles are kept.

## Setup mode

The base URL is `http://192.168.4.1` on the `OpenComfort-Setup` network.

| Method | Path | Description |
|---|---|---|
| `GET` | `/` | Setup wizard |
| `GET` | `/api/setup/products` | Products from the manifest |
| `POST` | `/api/setup` | Submit setup. See [Configuration › Setup request](configuration.md#setup-request) |

### `GET /api/setup/products`

```json
{
  "products": [
    { "id": "mitsubishi_heavy_industries", "name": "Mitsubishi Heavy Industries", "module": "ac", "bus": "spi", "version": "1.0.0" }
  ]
}
```

### `POST /api/setup`

On success it returns `{"ok": true}`, and the device continues to normal mode. Your client loses its connection when the setup access point closes, which is expected.
