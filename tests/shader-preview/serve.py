"""Shader-Vorschau (Host): rendert den Bildschirm-Shader aus src/quad_renderer.cpp per WebGL2
(= GLSL ES 3.00 wie auf der Quest) mit einem 320x200-Testmuster und legt die Bilder unter
tests/out/ ab.

    python tests/shader-preview/serve.py        # aus dem Repo-Stamm, dann im Browser:
    http://localhost:8765/tests/shader-preview/  # rendert und speichert automatisch

Der Server liefert nur Dateien aus dem Repo und nimmt nur PNG-Uploads mit festem
Namensmuster (shader-*.png) nach tests/out/ an; er lauscht nur auf 127.0.0.1.
"""
import http.server
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "tests", "out")
PORT = int(os.environ.get("PORT", "8765"))


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_POST(self):
        m = re.fullmatch(r"/save/(shader-[a-z0-9-]+\.(png|txt))", self.path)
        length = int(self.headers.get("Content-Length", "0"))
        if not m or length <= 0 or length > 64 * 1024 * 1024:
            self.send_error(400, "nur /save/shader-<name>.png|txt")
            return
        data = self.rfile.read(length)
        os.makedirs(OUT, exist_ok=True)
        with open(os.path.join(OUT, m.group(1)), "wb") as f:
            f.write(data)
        print("gespeichert:", os.path.join("tests", "out", m.group(1)), len(data), "Bytes", flush=True)
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"ok")


if __name__ == "__main__":
    print(f"Shader-Vorschau: http://localhost:{PORT}/tests/shader-preview/", flush=True)
    http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
    sys.exit(0)
