#!/usr/bin/env python3
"""Package a tool SD-root directory as a firmware-friendly ZIP release."""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
import zipfile
from pathlib import Path
from urllib.parse import urljoin

from manifest_signing import add_manifest_signature


SKIPPED_NAMES = {".DS_Store"}
SKIPPED_PREFIXES = ("._",)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def should_skip(path: Path) -> bool:
    name = path.name
    return name in SKIPPED_NAMES or any(name.startswith(prefix) for prefix in SKIPPED_PREFIXES)


def validate_tools_json(source_dir: Path) -> None:
    tools_json = source_dir / "tools.json"
    if not tools_json.is_file():
        raise SystemExit(f"tools.json not found: {tools_json}")
    with tools_json.open("r", encoding="utf-8") as f:
        payload = json.load(f)
    if not isinstance(payload.get("categories"), list):
        raise SystemExit("tools.json must contain a categories array")


def iter_entries(source_dir: Path) -> list[Path]:
    entries: list[Path] = []
    for root, dirs, files in os.walk(source_dir):
        root_path = Path(root)
        dirs[:] = sorted(d for d in dirs if not should_skip(root_path / d))
        for file_name in sorted(files):
            path = root_path / file_name
            if should_skip(path):
                continue
            entries.append(path)
    return entries


def write_zip(source_dir: Path, output_zip: Path) -> None:
    output_zip.parent.mkdir(parents=True, exist_ok=True)
    if output_zip.exists():
        output_zip.unlink()

    with zipfile.ZipFile(output_zip, "w", compression=zipfile.ZIP_STORED, allowZip64=False) as zf:
        for path in iter_entries(source_dir):
            arcname = path.relative_to(source_dir).as_posix()
            zf.write(path, arcname, compress_type=zipfile.ZIP_STORED)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", required=True, type=Path, help="Tool SD-root directory")
    parser.add_argument("--output-zip", required=True, type=Path, help="Output tools.zip path")
    parser.add_argument("--manifest", required=True, type=Path, help="Output manifest.json path")
    parser.add_argument("--version", required=True, help="Release version, e.g. 20260610-001")
    parser.add_argument("--base-url", required=True, help="Public HTTPS base URL, e.g. https://ota.example.com/")
    parser.add_argument("--package-name", default="tools/tools.zip", help="Published ZIP path")
    parser.add_argument("--release-date", default=dt.date.today().isoformat())
    parser.add_argument("--signing-key", type=Path, help="ES256 private key for manifest signing")
    parser.add_argument("--signature-key-id", help="Signing key identifier stored in the manifest")
    args = parser.parse_args()

    source_dir = args.source_dir.resolve()
    if not source_dir.is_dir():
        raise SystemExit(f"source directory not found: {source_dir}")
    validate_tools_json(source_dir)

    output_zip = args.output_zip.resolve()
    write_zip(source_dir, output_zip)

    base_url = args.base_url.rstrip("/") + "/"
    package_url = urljoin(base_url, args.package_name)
    manifest = {
        "version": args.version,
        "release_date": args.release_date,
        "package_url": package_url,
        "tools_url": package_url,
        "format": "zip-store",
        "size": output_zip.stat().st_size,
        "sha256": sha256_file(output_zip),
    }
    if args.signing_key or args.signature_key_id:
        if not args.signing_key or not args.signature_key_id:
            raise SystemExit("--signing-key and --signature-key-id must be used together")
        add_manifest_signature(
            manifest,
            kind="tools",
            private_key=args.signing_key.resolve(),
            key_id=args.signature_key_id,
        )

    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {output_zip}")
    print(f"wrote {args.manifest}")
    print(json.dumps(manifest, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
