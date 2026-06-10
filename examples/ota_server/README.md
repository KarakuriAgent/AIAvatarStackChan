# OTA Server

Small authenticated OTA server for AIAvatarStackChan firmware and tool updates.
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
so the version shown on the device matches the generated manifest. The same
generated header bakes these default update URLs into the firmware:

```text
<OTA_PUBLIC_BASE_URL>/manifest.json
<OTA_PUBLIC_BASE_URL>/tools/manifest.json
```

`config.json` can still override them with `ota_manifest_url` and
`tool_manifest_url`.

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

## Stage Tool Release

Tool updates use the same server and Bearer token, but a separate endpoint:

```text
/tools/manifest.json
/tools/tools.zip
```

Stage a tool SD-root release:

```sh
examples/ota_server/stage_tools.sh
```

By default, `stage_tools.sh` uses `TOOL_SOURCE_DIR` from `.env`, or the first
existing directory containing `tools.json` from `private/tool_assets/sdroot`,
and `sdcard`. It uses `OTA_PUBLIC_BASE_URL` and `OTA_ROOT` from `.env`, so an
already running server will serve the newly staged files as soon as they are
written.

The script creates a store-only ZIP archive so the firmware can extract it
without a deflate library. The device downloads `/tools/tools.zip`, verifies
size and SHA-256 from `/tools/manifest.json`, validates the packaged
`tools.json`, then expands the package to the SD card root.

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
  "tool_manifest_url": "https://ota.example.com/tools/manifest.json",
  "ota_api_key": "change-me"
}
```

Firmware and tool update files require:

```http
Authorization: Bearer <ota_api_key>
```
