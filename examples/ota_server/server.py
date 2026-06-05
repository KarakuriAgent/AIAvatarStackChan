#!/usr/bin/env python3
"""Small authenticated OTA file server.

Run this behind Cloudflare HTTPS. The local server itself speaks HTTP and checks
Authorization: Bearer <OTA_API_KEY> for every request.
"""

from __future__ import annotations

import argparse
import hmac
import mimetypes
import os
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from urllib.parse import unquote, urlparse


class OtaRequestHandler(SimpleHTTPRequestHandler):
    server_version = "AIAvatarOtaServer/1.0"

    def _authorized(self) -> bool:
        expected = self.server.api_key  # type: ignore[attr-defined]
        if not expected:
            return True
        header = self.headers.get("Authorization", "")
        prefix = "Bearer "
        if not header.startswith(prefix):
            return False
        return hmac.compare_digest(header[len(prefix):], expected)

    def do_GET(self) -> None:  # noqa: N802 - http.server API
        if not self._authorized():
            self.send_response(401)
            self.send_header("WWW-Authenticate", "Bearer")
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.end_headers()
            self.wfile.write(b"Unauthorized\n")
            return
        super().do_GET()

    def do_HEAD(self) -> None:  # noqa: N802 - http.server API
        if not self._authorized():
            self.send_response(401)
            self.send_header("WWW-Authenticate", "Bearer")
            self.end_headers()
            return
        super().do_HEAD()

    def translate_path(self, path: str) -> str:
        root = self.server.ota_root.resolve()  # type: ignore[attr-defined]
        parsed = urlparse(path)
        rel = unquote(parsed.path).lstrip("/") or "manifest.json"
        candidate = (root / rel).resolve()
        if root not in candidate.parents and candidate != root:
            return str(root / "__forbidden__")
        return str(candidate)

    def guess_type(self, path: str) -> str:
        if path.endswith(".bin"):
            return "application/octet-stream"
        if path.endswith(".json"):
            return "application/json"
        return mimetypes.guess_type(path)[0] or "application/octet-stream"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=os.environ.get("OTA_HOST", "0.0.0.0"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("OTA_PORT", "8080")))
    parser.add_argument("--root", type=Path, default=Path(os.environ.get("OTA_ROOT", "dist")))
    parser.add_argument("--api-key", default=os.environ.get("OTA_API_KEY", ""))
    args = parser.parse_args()

    args.root.mkdir(parents=True, exist_ok=True)
    httpd = ThreadingHTTPServer((args.host, args.port), OtaRequestHandler)
    httpd.ota_root = args.root
    httpd.api_key = args.api_key
    print(f"Serving OTA files from {args.root.resolve()} on http://{args.host}:{args.port}")
    print("Authentication:", "enabled" if args.api_key else "disabled")
    httpd.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
