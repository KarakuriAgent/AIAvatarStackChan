#!/usr/bin/env python3
"""Generate OTA manifest.json from a built firmware.bin."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
from urllib.parse import urljoin


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", required=True, type=Path, help="Path to firmware.bin")
    parser.add_argument("--version", required=True, help="Release version, e.g. 0.1.2")
    parser.add_argument("--base-url", required=True, help="Public HTTPS base URL, e.g. https://ota.example.com/")
    parser.add_argument("--firmware-name", default="firmware.bin", help="Published firmware file name")
    parser.add_argument("--release-date", default=dt.date.today().isoformat())
    parser.add_argument("--output", type=Path, default=Path("manifest.json"))
    args = parser.parse_args()

    firmware = args.firmware.resolve()
    if not firmware.is_file():
        raise SystemExit(f"firmware not found: {firmware}")

    base_url = args.base_url.rstrip("/") + "/"
    manifest = {
        "version": args.version,
        "release_date": args.release_date,
        "firmware_url": urljoin(base_url, args.firmware_name),
        "size": firmware.stat().st_size,
        "sha256": sha256_file(firmware),
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {args.output}")
    print(json.dumps(manifest, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
