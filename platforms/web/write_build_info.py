"""Record the successfully linked Web runtime and its exact module bytes."""

import argparse
import hashlib
import json
import os
from pathlib import Path


def write(config, module, output):
    info = json.loads(config.read_text(encoding="utf-8"))
    info["checksums"] = {}
    for name in info["files"]:
        with (module.parent / name).open("rb") as stream:
            info["checksums"][name] = hashlib.file_digest(stream, "sha256").hexdigest()
    temporary = output.with_suffix(".tmp")
    temporary.write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    os.replace(temporary, output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--module", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    write(args.config, args.module, args.output)
