#!/usr/bin/env python3
"""Local HTTP test server fixture for netlib integration tests.

Endpoints:
  GET  /            -> 200 "Hello World"
  GET  /notfound    -> 404 with header X-Custom: value
  POST /echo        -> echoes back request body with 200
  POST /echo_json   -> echoes back JSON body, asserts Content-Type application/json

Usage: python3 test_server.py --port 8080
"""

import argparse
import json
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _send(self, code, body, extra_headers=None):
        data = body.encode("utf-8") if isinstance(body, str) else body
        self.send_response(code)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Content-Type", "text/plain")
        if extra_headers:
            for name, value in extra_headers:
                self.send_header(name, value)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def do_GET(self):
        if self.path == "/" or self.path == "/index.html":
            self._send(200, "Hello World")
        elif self.path == "/notfound":
            self._send(404, "Not Found", [("X-Custom", "value")])
        elif self.path == "/redirect":
            self.send_response(302)
            self.send_header("Location", "/redirected")
            self.send_header("Content-Length", "0")
            self.end_headers()
        elif self.path == "/redirected":
            self._send(200, "redirected ok")
        elif self.path == "/duplicates":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Set-Cookie", "a=1; Path=/")
            self.send_header("Set-Cookie", "b=2; Path=/")
            self.send_header("X-Case-Test", "vAlUe")
            self.send_header("Content-Length", "2")
            self.end_headers()
            self.wfile.write(b"ok")
        elif self.path == "/slow":
            import time
            time.sleep(5)
            self._send(200, "slow response")
        else:
            self._send(404, "Not Found")

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0) or 0)
        body = self.rfile.read(length).decode("utf-8", "replace")
        if self.path == "/echo":
            self._send(200, body)
        elif self.path == "/echo_json":
            content_type = self.headers.get("Content-Type", "")
            if "application/json" not in content_type:
                self._send(400, "expected application/json")
                return
            try:
                parsed = json.loads(body)
                self._send(200, json.dumps(parsed))
            except ValueError:
                self._send(400, "invalid json")
        else:
            self._send(404, "Not Found")

    def log_message(self, fmt, *args):
        pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    sys.stderr.write(f"[test_server] listening on 127.0.0.1:{args.port}\n")
    sys.stderr.flush()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
