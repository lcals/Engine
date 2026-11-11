"""Copy official TypeScript and its installed matching platform compiler for offline fixtures."""

import json
from pathlib import Path
import shutil


def copy_typescript_toolchain(compiler, destination):
    compiler = Path(compiler)
    destination = Path(destination)
    definition = json.loads((compiler / "package.json").read_text(encoding="utf-8"))
    shutil.copytree(compiler, destination / "typescript")
    installed = 0
    for name, version in definition.get("optionalDependencies", {}).items():
        if not name.startswith("@typescript/typescript-"):
            continue
        source = compiler.parent / name
        if not source.is_dir():
            continue
        package = json.loads((source / "package.json").read_text(encoding="utf-8"))
        if package["version"] != version:
            raise AssertionError("TypeScript platform compiler version does not match its package")
        shutil.copytree(source, destination / name)
        installed += 1
    if installed == 0:
        raise AssertionError("Install the TypeScript native platform compiler with npm ci first")
