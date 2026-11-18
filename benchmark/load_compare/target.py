#!/usr/bin/env python3
"""Tiny local HTTP target for Lux / k6 / Locust comparisons."""

from __future__ import annotations

import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


BODY = b'{"ok":true,"service":"lux-load-compare"}\n'
GRAPHQL_OK = b'{"data":{"ping":"pong"}}\n'


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt: str, *args) -> None:
        # Quiet by default; comparisons measure clients, not log I/O.
        pass

    def _send(self, code: int, body: bytes, content_type: str = "application/json") -> None:
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        if self.path.split("?", 1)[0] in ("/", "/health", "/hello"):
            self._send(200, BODY)
            return
        self._send(404, b'{"ok":false,"error":"not found"}\n')

    def do_POST(self) -> None:
        length = int(self.headers.get("Content-Length", "0") or "0")
        raw = self.rfile.read(length) if length > 0 else b""
        path = self.path.split("?", 1)[0]
        if path == "/echo":
            self._send(200, raw if raw else BODY)
            return
        if path == "/graphql":
            # Always 200 with data; mirrors common GraphQL HTTP behavior.
            try:
                json.loads(raw.decode("utf-8") or "{}")
            except Exception:
                self._send(200, b'{"errors":[{"message":"bad json"}]}\n')
                return
            self._send(200, GRAPHQL_OK)
            return
        self._send(404, b'{"ok":false,"error":"not found"}\n')


class TargetServer(ThreadingHTTPServer):
    allow_reuse_address = True
    # Lux opens one process per VU with Connection: close; keep the accept
    # queue deep so short compare runs do not drown in status=0 dial failures.
    request_queue_size = 1024


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8765)
    args = ap.parse_args()
    httpd = TargetServer((args.host, args.port), Handler)
    print(f"LOAD_COMPARE_TARGET=http://{args.host}:{args.port}", flush=True)
    httpd.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
