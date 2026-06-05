# OTA Server

Small authenticated OTA server for AIAvatarStackChan firmware updates.
Run it behind Cloudflare HTTPS. The device accesses the Cloudflare HTTPS URL,
while this local server can stay plain HTTP behind the tunnel/proxy.

## Build and Stage OTA Release

```sh
examples/ota_server/build_ota.sh
```

By default, this generates a timestamp version like `20260606-153012`, builds
`examples/stackchan/basic`, copies `firmware.bin` to `examples/ota_server/dist`,
and writes `manifest.json` with the same version. You can override values:

```sh
examples/ota_server/build_ota.sh \
  --version 0.1.1 \
  --release-date 2026-06-06 \
  --base-url https://ota.example.com/
```

The script also writes `src/FirmwareInfoGenerated.h`, which is ignored by git,
so the version shown on the device matches the generated manifest.

## Generate manifest only

```sh
python3 examples/ota_server/generate_manifest.py \
  --firmware examples/stackchan/basic/.pio/build/cores3/firmware.bin \
  --version 0.1.1 \
  --release-date 2026-06-06 \
  --base-url https://ota.example.com/ \
  --output examples/ota_server/dist/manifest.json
cp examples/stackchan/basic/.pio/build/cores3/firmware.bin examples/ota_server/dist/firmware.bin
```

## Serve

You can use `examples/ota_server/.env.example` as a template for environment variables.
By default it binds to `127.0.0.1`, so point Cloudflare Tunnel to `http://127.0.0.1:8080`.
The current server reads environment variables directly; it does not auto-load `.env` files.

```sh
cp examples/ota_server/.env.example examples/ota_server/.env
# Edit examples/ota_server/.env and set OTA_API_KEY.
examples/ota_server/start.sh
examples/ota_server/status.sh
examples/ota_server/stop.sh
```

Manual start also works:

```sh
OTA_API_KEY='change-me' python3 examples/ota_server/server.py --root examples/ota_server/dist --port 8080
```

Device config:

```json
{
  "ota_manifest_url": "https://ota.example.com/manifest.json",
  "ota_api_key": "change-me"
}
```

Both `manifest.json` and `firmware.bin` require:

```http
Authorization: Bearer <ota_api_key>
```
