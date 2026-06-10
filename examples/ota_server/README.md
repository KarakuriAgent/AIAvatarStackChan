# OTA Server

Small authenticated OTA server for AIAvatarStackChan firmware and tool updates.
Run it behind Cloudflare HTTPS. The device accesses the Cloudflare HTTPS URL,
while this local server can stay plain HTTP behind the tunnel/proxy.

OTA authenticity is checked with signed firmware and tool manifests. The HTTPS
certificate is not the trust root for updates; the firmware verifies manifest
signatures with the public key bundled as `/ota_trust.json`.

## Signing Key

Create one ES256 signing key and keep it out of `dist/`:

```sh
mkdir -p examples/ota_server/secrets
openssl ecparam -genkey -name prime256v1 -noout \
  -out examples/ota_server/secrets/ota_signing_private_key.pem
```

`examples/ota_server/secrets/` is ignored by git. The build scripts use
`OTA_SIGNING_KEY` and `OTA_SIGNATURE_KEY_ID` from `.env` when set, otherwise
`secrets/ota_signing_private_key.pem` and `main-2026`.

## Build and Stage OTA Release

```sh
examples/ota_server/build_ota.sh
```

By default, this generates a timestamp version like `20260606-153012`, writes
`src/FirmwareInfoGenerated.h`, generates `private/firmware_assets/sdroot/ota_trust.json`
from the signing key, builds `examples/stackchan/basic`, copies `firmware.bin`
to `examples/ota_server/dist`, and writes signed `manifest.json` with the same
version. You can override values:

```sh
examples/ota_server/build_ota.sh \
  --version 0.1.1 \
  --release-date 2026-06-06 \
  --base-url https://ota.example.com/
```

The generated header bakes these default update URLs into the firmware:

```text
<OTA_PUBLIC_BASE_URL>/manifest.json
<OTA_PUBLIC_BASE_URL>/tools/manifest.json
```

`config.json` can still override them with `ota_manifest_url` and
`tool_manifest_url`. Manifest signatures still have to verify.

## Generate Manifest Only

```sh
python3 examples/ota_server/generate_manifest.py \
  --firmware examples/stackchan/basic/.pio/build/cores3/firmware.bin \
  --version 0.1.1 \
  --release-date 2026-06-06 \
  --base-url https://ota.example.com/ \
  --signing-key examples/ota_server/secrets/ota_signing_private_key.pem \
  --signature-key-id main-2026 \
  --output examples/ota_server/dist/manifest.json
cp examples/stackchan/basic/.pio/build/cores3/firmware.bin examples/ota_server/dist/firmware.bin
```

## Stage Tool Release

Tool updates use the same server, Bearer token, and signing key, but a separate endpoint:

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
and `sdcard`. It uses `OTA_PUBLIC_BASE_URL`, `OTA_ROOT`, `OTA_SIGNING_KEY`, and
`OTA_SIGNATURE_KEY_ID` from `.env`, so an already running server will serve the
newly staged files as soon as they are written.

The script creates a store-only ZIP archive so the firmware can extract it
without a deflate library. The device downloads `/tools/manifest.json`, verifies
its signature, downloads `/tools/tools.zip`, verifies size and SHA-256, validates
the packaged `tools.json`, then expands the package to the SD card root.

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
