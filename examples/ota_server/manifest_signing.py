#!/usr/bin/env python3
"""Helpers for signing AIAvatarStackChan OTA manifests."""

from __future__ import annotations

import base64
import subprocess
from pathlib import Path
from typing import Any


SIGNATURE_ALG = "es256"
SIGNATURE_PAYLOAD_PREFIX = "aiavatar-ota-signature-v1"


def canonical_payload(kind: str, manifest: dict[str, Any]) -> bytes:
    if kind == "firmware":
        url = manifest["firmware_url"]
    elif kind == "tools":
        url = manifest.get("tools_url") or manifest["package_url"]
    else:
        raise ValueError(f"unsupported manifest kind: {kind}")

    text = "\n".join(
        [
            SIGNATURE_PAYLOAD_PREFIX,
            f"kind={kind}",
            f"version={manifest['version']}",
            f"release_date={manifest['release_date']}",
            f"url={url}",
            f"size={int(manifest['size'])}",
            f"sha256={manifest['sha256']}",
            "",
        ]
    )
    return text.encode("utf-8")


def sign_payload(payload: bytes, private_key: Path) -> str:
    result = subprocess.run(
        ["openssl", "dgst", "-sha256", "-sign", str(private_key)],
        input=payload,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        stderr = result.stderr.decode("utf-8", errors="replace").strip()
        raise SystemExit(f"openssl signing failed: {stderr}")
    return base64.b64encode(result.stdout).decode("ascii")


def add_manifest_signature(
    manifest: dict[str, Any],
    *,
    kind: str,
    private_key: Path,
    key_id: str,
) -> None:
    if not private_key.is_file():
        raise SystemExit(f"signing private key not found: {private_key}")
    manifest["signature_key_id"] = key_id
    manifest["signature_alg"] = SIGNATURE_ALG
    manifest["signature"] = sign_payload(canonical_payload(kind, manifest), private_key)


def public_key_der_base64(private_key: Path) -> str:
    result = subprocess.run(
        ["openssl", "pkey", "-in", str(private_key), "-pubout", "-outform", "DER"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        stderr = result.stderr.decode("utf-8", errors="replace").strip()
        raise SystemExit(f"openssl public key export failed: {stderr}")
    return base64.b64encode(result.stdout).decode("ascii")
