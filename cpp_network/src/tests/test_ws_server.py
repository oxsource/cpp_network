#!/usr/bin/env python3
"""Local WebSocket echo test fixture for cpp_network specs/006 tests.

Minimal hand-rolled RFC 6455 server (stdlib only) so host gtest and on-device
runs share one deterministic peer. Modes:

  plain          default; ws:// endpoint (no TLS)
  --tls          wss:// using src/tests/certs/server_{cert,key}.pem
  --require-client-cert      mTLS variant of the wss:// endpoint
  --inject-ping              send an unsolicited ping before echoing each
                             message (transparency assertions, US2)
  --peer-close CODE REASON   immediately close with CODE/REASON after the
                             handshake completes (US3 detail-carrying close)
  --fragment-size N          split outgoing echoes into N-byte fragments

Usage:
  python3 test_ws_server.py --port 18086
  python3 test_ws_server.py --port 18446 --tls [--require-client-cert]
"""

import argparse
import base64
import hashlib
import os
import socket
import ssl
import struct
import sys
import threading

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

OP_CONT = 0x0
OP_TEXT = 0x1
OP_BIN = 0x2
OP_CLOSE = 0x8
OP_PING = 0x9
OP_PONG = 0xA


def _recv_exact(conn, n):
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("closed mid-frame")
        buf += chunk
    return buf


def read_frame(conn):
    hdr = _recv_exact(conn, 2)
    fin = bool(hdr[0] & 0x80)
    opcode = hdr[0] & 0x0F
    masked = bool(hdr[1] & 0x80)
    length = hdr[1] & 0x7F
    if length == 126:
        length = struct.unpack(">H", _recv_exact(conn, 2))[0]
    elif length == 127:
        length = struct.unpack(">Q", _recv_exact(conn, 8))[0]
    mask = _recv_exact(conn, 4) if masked else None
    payload = _recv_exact(conn, length) if length else b""
    if mask:
        payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
    return fin, opcode, payload


def send_frame(conn, opcode, payload=b"", fragment=0):
    pieces = [payload] if not fragment else [
        payload[i:i + fragment] for i in range(0, len(payload), fragment)
    ] or [b""]
    for i, piece in enumerate(pieces):
        op = opcode if i == 0 else OP_CONT
        fin = 0x80 if i == len(pieces) - 1 else 0x0
        header = bytearray([fin | op])
        # Second byte holds only the length here: the MASK bit is set
        # exclusively on client->server frames.
        if len(piece) < 126:
            header.append(len(piece))
        elif len(piece) <= 0xFFFF:
            header.append(126)
            header += struct.pack(">H", len(piece))
        else:
            header.append(127)
            header += struct.pack(">Q", len(piece))
        conn.sendall(bytes(header) + piece)


def handshake(conn):
    req = b""
    while b"\r\n\r\n" not in req:
        chunk = conn.recv(4096)
        if not chunk:
            raise ConnectionError("no handshake")
        req += chunk
    headers = {}
    lines = req.decode("latin1").split("\r\n")
    for line in lines[1:]:
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip().lower()] = v.strip()
    key = headers.get("sec-websocket-key", "")
    accept = base64.b64encode(
        hashlib.sha1((key + GUID).encode()).digest()).decode()
    resp = ("HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\nConnection: Upgrade\r\n"
            f"Sec-WebSocket-Accept: {accept}\r\n\r\n")
    conn.sendall(resp.encode())


def serve_connection(conn, args):
    handshake(conn)
    if args.peer_close > 0:
        reason = (args.reason or "").encode()
        body = struct.pack(">H", args.peer_close) + reason
        send_frame(conn, OP_CLOSE, body[:125])
        try:
            read_frame(conn)
        except ConnectionError:
            pass
        return

    pending = {"pinged": False}
    msg = {"opcode": None, "data": None}
    while True:
        try:
            fin, opcode, payload = read_frame(conn)
            if os.environ.get("WS_FIXTURE_TRACE"):
                sys.stderr.write(f"[fx] op={opcode} fin={fin} len={len(payload)} "
                                 f"{payload[:32].hex()}\n")
                sys.stderr.flush()
        except (ConnectionError, OSError):
            return
        if opcode == OP_PING:
            send_frame(conn, OP_PONG, payload)
            continue
        if opcode == OP_PONG:
            continue
        if opcode == OP_CLOSE:
            code = struct.unpack(">H", payload[:2])[0] if len(payload) >= 2 else 1005
            reply = struct.pack(">H", code if code != 1005 else 1000)
            send_frame(conn, OP_CLOSE, reply)
            return
        if opcode in (OP_TEXT, OP_BIN):
            msg["opcode"] = opcode
            msg["data"] = bytearray(payload)
        elif opcode == OP_CONT and msg["data"] is not None:
            msg["data"].extend(payload)

        if not fin or msg["data"] is None:
            continue

        if args.inject_ping and not pending["pinged"]:
            pending["pinged"] = True
            send_frame(conn, OP_PING, b"probe")

        out_opcode = msg["opcode"]
        data = bytes(msg["data"])
        msg = {}
        send_frame(conn, out_opcode, data,
                   fragment=args.fragment_size or 0)


def _build_tls_context(args):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(
        certfile=os.path.join(args.certs_dir, "server_cert.pem"),
        keyfile=os.path.join(args.certs_dir, "server_key.pem"))
    if args.require_client_cert:
        ctx.verify_mode = ssl.CERT_REQUIRED
        ctx.load_verify_locations(
            cafile=os.path.join(args.certs_dir, "ca_cert.pem"))
    return ctx


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--tls", action="store_true")
    parser.add_argument("--certs-dir", default="src/tests/certs")
    parser.add_argument("--require-client-cert", action="store_true")
    parser.add_argument("--inject-ping", action="store_true")
    parser.add_argument("--peer-close", type=int, default=0)
    parser.add_argument("--reason", default="")
    parser.add_argument("--fragment-size", type=int, default=0)
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("127.0.0.1", args.port))
    sock.listen(8)
    sys.stderr.write(f"[test_ws_server] listening on 127.0.0.1:{args.port} "
                     f"tls={args.tls}\n")
    sys.stderr.flush()

    tls_ctx = _build_tls_context(args) if args.tls else None

    def serve(conn):
        try:
            if tls_ctx is not None:
                conn = tls_ctx.wrap_socket(conn, server_side=True)
            serve_connection(conn, args)
        except (ConnectionError, ssl.SSLError, OSError):
            # Readiness probes close pre-handshake; broken peers must not
            # take the fixture down.
            pass
        finally:
            try:
                conn.close()
            except OSError:
                pass

    while True:
        try:
            conn, _ = sock.accept()
        except OSError:
            continue
        threading.Thread(target=serve, args=(conn,), daemon=True).start()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
