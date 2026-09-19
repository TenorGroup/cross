#!/usr/bin/env python3
"""HTTP/1.1 fixture for X3 transport acceptance; local server, no upstream I/O."""
import argparse
import itertools
import json
import select
import socket
import socketserver
import threading
import time
from urllib.parse import parse_qs, urlsplit

PAYLOAD = b"tenor-cross transport acceptance fixture\n" * 8
COUNTER = itertools.count(1)
LOG_LOCK = threading.Lock()


def log(event, request_id=0, **fields):
    with LOG_LOCK:
        print(json.dumps({"monotonic_s": round(time.monotonic(), 6), "event": event,
                          "request_id": request_id, **fields}), flush=True)


class FixtureServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, address, hold_seconds=3.2):
        if hold_seconds < 3:
            raise ValueError("hold_seconds must be at least 3")
        self.hold_seconds = hold_seconds
        super().__init__(address, FixtureHandler)


class FixtureHandler(socketserver.BaseRequestHandler):
    def send(self, data, part):
        self.request.sendall(data)
        log("sent", self.ident, part=part, bytes=len(data), path=self.path)

    def hold(self, seconds, phase):
        log("holding", self.ident, phase=phase, seconds=seconds, path=self.path)
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            ready, _, _ = select.select([self.request], [], [], min(.1, max(0, deadline - time.monotonic())))
            if ready:
                if not self.request.recv(1, socket.MSG_PEEK):
                    log("peer_closed", self.ident, path=self.path)
                    return False
                # This fixture accepts one request per connection. A pipelined
                # request must not turn the hold loop into a CPU busy loop.
                time.sleep(min(.01, max(0, deadline - time.monotonic())))
        return True

    def headers(self, status, extra=(), close=False):
        fields = [f"HTTP/1.1 {status}", "Server: tenor-local-fixture",
                  "Connection: close" if close else "Connection: keep-alive", *extra, "", ""]
        self.send("\r\n".join(fields).encode("ascii"), "headers")

    def chunk(self, data):
        self.send(f"{len(data):x}\r\n".encode("ascii") + data + b"\r\n", "chunk")

    def handle(self):
        self.ident = next(COUNTER)
        self.path = ""
        log("connected", self.ident, peer=self.client_address)
        self.request.settimeout(5)
        try:
            raw = b""
            while b"\r\n\r\n" not in raw:
                data = self.request.recv(1024)
                if not data:
                    return
                raw += data
                if len(raw) > 8192:
                    raise ValueError("request headers exceed 8192 bytes")
            method, target, version = raw.split(b"\r\n", 1)[0].decode("ascii").split(" ")
            parsed = urlsplit(target)
            self.path = parsed.path
            log("request", self.ident, method=method, target=target, version=version)
            if method not in ("GET", "HEAD") or version not in ("HTTP/1.0", "HTTP/1.1"):
                self.headers("405 Method Not Allowed", ["Content-Length: 0"], close=True)
                return
            if self.path == "/disconnect":
                return
            if self.path == "/stall-header" and not self.hold(self.server.hold_seconds, "before-headers"):
                return
            if self.path in ("/204", "/304"):
                self.headers("204 No Content" if self.path == "/204" else "304 Not Modified")
            elif self.path == "/interim204":
                self.send(b"HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 103 Early Hints\r\n\r\n", "interim-headers")
                self.headers("204 No Content")
            elif self.path in ("/fixed", "/head", "/stall-header", "/stall-fixed", "/incomplete-fixed"):
                self.headers("200 OK", [f"Content-Length: {len(PAYLOAD)}", "Content-Type: application/octet-stream"])
                if method != "HEAD":
                    if self.path in ("/stall-fixed", "/incomplete-fixed"):
                        self.send(PAYLOAD[:16], "body-prefix")
                        if self.path == "/incomplete-fixed":
                            return
                        if not self.hold(self.server.hold_seconds, "mid-fixed-body"):
                            return
                        self.send(PAYLOAD[16:], "body-rest")
                    else:
                        self.send(PAYLOAD, "body")
            elif self.path in ("/chunked", "/stall-chunked", "/incomplete-chunked"):
                self.headers("200 OK", ["Transfer-Encoding: chunked", "Content-Type: application/octet-stream"])
                if method != "HEAD":
                    slow_ms = int(parse_qs(parsed.query).get("slow_ms", ["0"])[0])
                    if not 0 <= slow_ms <= 1000:
                        raise ValueError("slow_ms must be between 0 and 1000")
                    for index in range(0, len(PAYLOAD), 16):
                        self.chunk(PAYLOAD[index:index + 16])
                        if index == 0 and self.path == "/incomplete-chunked":
                            return
                        pause = self.server.hold_seconds if index == 0 and self.path == "/stall-chunked" else slow_ms / 1000
                        if pause and not self.hold(pause, "between-chunks"):
                            return
                    self.send(b"0\r\n\r\n", "chunk-terminator")
            elif self.path in ("/close", "/stall-close"):
                self.headers("200 OK", ["Content-Type: application/octet-stream"], close=True)
                if method != "HEAD":
                    if self.path == "/stall-close":
                        self.send(PAYLOAD[:16], "body-prefix")
                        if not self.hold(self.server.hold_seconds, "mid-close-body"):
                            return
                        self.send(PAYLOAD[16:], "body-rest")
                    else:
                        self.send(PAYLOAD, "body")
                return
            else:
                self.headers("404 Not Found", ["Content-Length: 0"], close=True)
                return
            self.hold(self.server.hold_seconds, "after-response")
        except (BrokenPipeError, ConnectionResetError):
            log("peer_closed", self.ident, path=self.path)
        except (TimeoutError, ValueError, OSError) as error:
            log("error", self.ident, path=self.path, error=str(error))
        finally:
            log("closed", self.ident, path=self.path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, help="Explicit local interface IP, e.g. 10.10.10.100")
    parser.add_argument("--port", required=True, type=int)
    parser.add_argument("--hold-seconds", type=float, default=3.2)
    args = parser.parse_args()
    with FixtureServer((args.host, args.port), args.hold_seconds) as server:
        log("listening", address=server.server_address, hold_seconds=args.hold_seconds,
            payload_bytes=len(PAYLOAD))
        try:
            server.serve_forever(poll_interval=.1)
        except KeyboardInterrupt:
            pass
        finally:
            log("stopped")


if __name__ == "__main__":
    main()
