#!/usr/bin/env python3
"""Local HTTP server for the net tests (test_http, test_fetch, test_dlq). 127.0.0.1 only.

Usage: net_server.py PORT FIXTURES_DIR

  /f/<name>          fixture file, honours "Range: bytes=N-" (206, 416 past the end)
  /norange/<name>    fixture file, ignores Range (always 200, whole file)
  /badrange/<name>   whole fixture as a 206 "bytes 0-..." whatever Range asked for
  /redirect/<name>   302 -> /f/<name>
  /auth/<name>       fixture file behind Basic user:pass (401 otherwise)
  /cd                minimal.epub with Content-Disposition filename*=UTF-8''%E6%97%A5%E6%9C%AC.epub
  /slow?n=N&ms=M     N bytes, one every M ms (Content-Length N)
  /big?n=N           N bytes with Content-Length; /bigchunked?n=N the same without one
  /flaky/<name>      503 the first time each name is asked for, then the fixture file
  /tag/<t>           body "<t>", and t is appended to the request log
  /tags              the request log, comma-joined, then cleared
  /404 and anything else: 404
Every /f/ request with a Range header logs "range:<start>".
"""
import base64
import http.server
import os
import sys
import threading
import time
import urllib.parse

ROOT = "."
LOCK = threading.Lock()
TAGS = []
FLAKY = set()
TYPES = {".png": "image/png", ".jpg": "image/jpeg", ".txt": "text/plain",
         ".epub": "application/epub+zip", ".webp": "image/webp"}


def log_tag(t):
    with LOCK:
        TAGS.append(t)


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, fmt, *args):  # quiet
        pass

    def send_body(self, code, body, ctype="application/octet-stream", extra=None, length=True):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        if length:
            self.send_header("Content-Length", str(len(body)))
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def not_found(self):
        self.send_body(404, b"not found", "text/plain")

    def read_fixture(self, name):
        name = urllib.parse.unquote(name)
        path = os.path.normpath(os.path.join(ROOT, name))
        if not path.startswith(os.path.normpath(ROOT) + os.sep) or not os.path.isfile(path):
            return None, None
        with open(path, "rb") as f:
            return f.read(), TYPES.get(os.path.splitext(path)[1].lower(), "application/octet-stream")

    def serve_file(self, name, ranges=True, extra=None):
        data, ctype = self.read_fixture(name)
        if data is None:
            return self.not_found()
        rng = self.headers.get("Range")
        if ranges and rng and rng.startswith("bytes=") and rng.endswith("-"):
            start = int(rng[6:-1])
            log_tag("range:%d" % start)
            if start >= len(data):
                return self.send_body(416, b"", "text/plain",
                                      {"Content-Range": "bytes */%d" % len(data)})
            return self.send_body(206, data[start:], ctype, dict(
                extra or {}, **{"Content-Range": "bytes %d-%d/%d" % (start, len(data) - 1, len(data))}))
        self.send_body(200, data, ctype, extra)

    def do_GET(self):
        u = urllib.parse.urlsplit(self.path)
        q = dict(urllib.parse.parse_qsl(u.query))
        p = u.path
        if p.startswith("/f/"):
            return self.serve_file(p[3:])
        if p.startswith("/badrange/"):  # 206 that always starts at byte 0
            data, ctype = self.read_fixture(p[10:])
            if data is None:
                return self.not_found()
            return self.send_body(206, data, ctype,
                                  {"Content-Range": "bytes 0-%d/%d" % (len(data) - 1, len(data))})
        if p.startswith("/norange/"):
            return self.serve_file(p[9:], ranges=False)
        if p.startswith("/redirect/"):
            self.send_response(302)
            self.send_header("Location", "/f/" + p[10:])
            self.send_header("Content-Length", "0")
            return self.end_headers()
        if p.startswith("/auth/"):
            want = "Basic " + base64.b64encode(b"user:pass").decode()
            if self.headers.get("Authorization") != want:
                return self.send_body(401, b"login", "text/plain",
                                      {"WWW-Authenticate": 'Basic realm="t"'})
            return self.serve_file(p[6:])
        if p == "/cd":
            return self.serve_file("minimal.epub", extra={
                "Content-Disposition":
                    "attachment; filename=\"fallback.epub\"; filename*=UTF-8''%E6%97%A5%E6%9C%AC.epub"})
        if p == "/slow":
            n, ms = int(q.get("n", "100")), int(q.get("ms", "100"))
            self.send_response(200)
            self.send_header("Content-Length", str(n))
            self.end_headers()
            try:
                for _ in range(n):
                    self.wfile.write(b"s")
                    self.wfile.flush()
                    time.sleep(ms / 1000.0)
            except (BrokenPipeError, ConnectionResetError):
                pass
            return None
        if p in ("/big", "/bigchunked"):
            return self.send_body(200, b"x" * int(q.get("n", "1000")), length=(p == "/big"))
        if p.startswith("/flaky/"):
            with LOCK:
                first = p not in FLAKY
                FLAKY.add(p)
            if first:
                return self.send_body(503, b"busy", "text/plain")
            return self.serve_file(p[7:])
        if p.startswith("/tag/"):
            log_tag(p[5:])
            return self.send_body(200, p[5:].encode(), "text/plain")
        if p == "/tags":
            with LOCK:
                body = ",".join(TAGS).encode()
                TAGS.clear()
            return self.send_body(200, body, "text/plain")
        return self.not_found()


def exit_with_parent():
    """A test that crashes never stops its server: leave when the parent is gone."""
    parent = os.getppid()
    while os.getppid() == parent:
        time.sleep(0.5)
    os._exit(0)


def main():
    global ROOT
    threading.Thread(target=exit_with_parent, daemon=True).start()
    port = int(sys.argv[1])
    ROOT = os.path.abspath(sys.argv[2])
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", port), Handler)
    srv.daemon_threads = True
    srv.serve_forever()


if __name__ == "__main__":
    main()
