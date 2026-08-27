#!/usr/bin/env python3
"""Local HTTPS test server fixture for cpp_network TLS integration tests.

Serves GET / with a self-signed certificate signed by src/tests/certs/ca_cert.pem.
Optionally requires client certificates (mTLS) with --require-client-cert.

Usage: python3 test_tls_server.py --port 18443 [--require-client-cert] \
       --certs-dir src/tests/certs
"""

import argparse
import os
import ssl
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        body = b"secure hello"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        pass


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=18443)
    parser.add_argument("--certs-dir", default="src/tests/certs")
    parser.add_argument("--require-client-cert", action="store_true")
    args = parser.parse_args()

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(
        certfile=os.path.join(args.certs_dir, "server_cert.pem"),
        keyfile=os.path.join(args.certs_dir, "server_key.pem"),
    )
    if args.require_client_cert:
        ctx.verify_mode = ssl.CERT_REQUIRED
        ctx.load_verify_locations(
            cafile=os.path.join(args.certs_dir, "ca_cert.pem"))

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    server.socket = ctx.wrap_socket(server.socket, server_side=True)
    sys.stderr.write(
        f"[test_tls_server] listening on 127.0.0.1:{args.port} "
        f"mTLS={args.require_client_cert}\n")
    sys.stderr.flush()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
