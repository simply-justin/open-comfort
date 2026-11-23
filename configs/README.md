# OpenComfort Profile Repository

This folder is the reference layout for the repository that OpenComfort devices read during setup. It holds a **manifest** of supported air conditioners and one **profile** per model.

Publish this folder as its own GitHub repository (for example `opencomfort/configs`). Devices then fetch:

```text
https://raw.githubusercontent.com/<owner>/<repo>/main/manifest.json
```

## Layout

```text
manifest.json                 # List of supported products
profiles/
  <product-id>.json           # One protocol profile per product
```

## Adding a product

1. Add `profiles/<product-id>.json`.
2. Add an entry to `manifest.json` with `id`, `name`, `module`, `bus`, `profileUrl`, `version`, and a `checksum`:

   ```sh
   echo "sha256:$(sha256sum profiles/<product-id>.json | cut -d' ' -f1)"
   ```

3. Update the `checksum` every time the profile changes. Otherwise devices reject the download.

For the full manifest and profile format, see [docs/profiles.md](../docs/profiles.md).
