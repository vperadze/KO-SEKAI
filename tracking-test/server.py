#!/usr/bin/env python3
"""Local server for the KO-SEKAI tracking test.

Serves the test page on http://127.0.0.1:8765 and saves guided-test recordings
(joint numbers only, no images) posted to /record into ./recordings/.
"""
import functools
import http.server
import json
import os
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
REC_DIR = os.path.join(ROOT, "recordings")
MAX_BYTES = 20_000_000


class Handler(http.server.SimpleHTTPRequestHandler):
    def do_POST(self):
        if self.path != "/record":
            self.send_error(404)
            return
        size = int(self.headers.get("Content-Length", "0"))
        if size <= 0 or size > MAX_BYTES:
            self.send_error(413)
            return
        body = self.rfile.read(size)
        try:
            json.loads(body)
        except ValueError:
            self.send_error(400, "Body is not JSON")
            return
        os.makedirs(REC_DIR, exist_ok=True)
        name = time.strftime("rec-%Y%m%d-%H%M%S.json")
        with open(os.path.join(REC_DIR, name), "wb") as handle:
            handle.write(body)
        reply = json.dumps({"saved": name}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(reply)))
        self.end_headers()
        self.wfile.write(reply)

    def end_headers(self):
        # Always serve the newest page, so a normal reload picks up changes.
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        # Request log next to this file, so a failed save can be diagnosed.
        with open(os.path.join(ROOT, "server.log"), "a") as log:
            log.write(time.strftime("%H:%M:%S ") + (fmt % args) + "\n")


if __name__ == "__main__":
    handler = functools.partial(Handler, directory=ROOT)
    http.server.ThreadingHTTPServer(("127.0.0.1", 8765), handler).serve_forever()
