#!/usr/bin/env python3
"""Generate firmware-bundled OTA trust settings from a signing key."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from manifest_signing import SIGNATURE_ALG, public_key_der_base64


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--private-key", required=True, type=Path)
    parser.add_argument("--key-id", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    if not args.private_key.is_file():
        raise SystemExit(f"signing private key not found: {args.private_key}")

    trust = {
        "ota_signing_public_keys": [
            {
                "id": args.key_id,
                "alg": SIGNATURE_ALG,
                "public_key": public_key_der_base64(args.private_key),
            }
        ]
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(trust, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
