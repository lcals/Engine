"""Serve one exported runtime on loopback with the headers required by pthread WebGPU."""

import argparse
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class Handler(SimpleHTTPRequestHandler):
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map,
                      ".mjs": "text/javascript", ".wasm": "application/wasm"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--port", type=int, default=8471)
    args = parser.parse_args()
    root = args.directory.resolve(strict=True)
    if not (root / "index.html").is_file():
        parser.error("The directory must contain an exported index.html")
    with ThreadingHTTPServer(("127.0.0.1", args.port), partial(Handler, directory=str(root))) as server:
        print(f"Web runtime: http://127.0.0.1:{server.server_port}/", flush=True)
        server.serve_forever()
