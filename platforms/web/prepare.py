"""Stage the pinned IDBFS transaction patch without modifying the installed SDK."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def prepare(sdk, output):
    root = Path(__file__).resolve().parents[2]
    config = json.loads((root / "platforms/web/toolchain.json").read_text())
    version = (sdk / "emscripten-version.txt").read_text().strip().strip('"')
    if version != config["emscripten"]:
        raise ValueError("The Web target requires Emscripten " + config["emscripten"])
    source = sdk / "src/lib/libidbfs.js"
    payload = source.read_bytes()
    if hashlib.sha256(payload).hexdigest() != config["idbfs_sha256"]:
        raise ValueError("The Emscripten IDBFS source does not match the pinned version")
    target = output / "src/lib/libidbfs.js"
    patch = root / "third_party/emscripten/idbfs-atomic-sync.patch"
    fingerprint = hashlib.sha256(payload + patch.read_bytes()).hexdigest()
    stamp = output / "fingerprint"
    if target.is_file() and stamp.is_file() and stamp.read_text() == fingerprint:
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(payload)
    subprocess.run(["git", "apply", "--directory", output.relative_to(root).as_posix(), str(patch)],
                   cwd=root, check=True)
    stamp.write_text(fingerprint)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    options = parser.parse_args()
    prepare(options.sdk.resolve(), options.output.resolve())
