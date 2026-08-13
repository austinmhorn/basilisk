#!/usr/bin/env python3
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import json
import os

PORT = 8765
ACTION_FILE = Path("basilisk_debug_action.txt")

class Handler(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        super().end_headers()

    def do_POST(self):
        if self.path != "/debug-action":
            self.send_error(404)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length) or b"{}")
            index = int(payload.get("index", 0))
            player = int(payload.get("player", 0))
            round_number = int(payload.get("round", 0))
            if index <= 0 or player <= 0 or round_number <= 0:
                raise ValueError("invalid debug action")
            tmp = ACTION_FILE.with_suffix(".tmp")
            tmp.write_text(f"{player} {round_number} {index}\n", encoding="utf-8")
            os.replace(tmp, ACTION_FILE)
            body = json.dumps({"ok": True, "index": index}).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except Exception as exc:
            body = json.dumps({"ok": False, "error": str(exc)}).encode("utf-8")
            self.send_response(400)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

if __name__ == "__main__":
    print(f"Basilisk visual debug server: http://localhost:{PORT}/clients/web-debug/index.html")
    print("Browser actions write to basilisk_debug_action.txt")
    ThreadingHTTPServer(("", PORT), Handler).serve_forever()
