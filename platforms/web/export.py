"""Export a WebGPU runtime with independent, validated project resources."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from artifact_output import plain_path, staged_output, validate_output
from resource_catalog import copy_resource_snapshot, parse_build_output, safe_file
from runtime_settings import export_runtime_settings


def catalog_accessor(flatc, node):
    sources = [ROOT / "include/schemas/runtime/asset_catalog.fbs"]
    sources += [Path(__file__).parent / name for name in
                ("catalog.ts", "build_catalog.mjs", "package-lock.json")]
    version = subprocess.run([str(flatc), "--version"], check=True,
                             capture_output=True).stdout
    fingerprint = hashlib.sha256(version + b"".join(p.read_bytes() for p in sources)).hexdigest()
    output = ROOT / "build/web-export-tools" / fingerprint
    if not (output / "catalog.mjs").is_file():
        subprocess.run([node, str(Path(__file__).with_name("build_catalog.mjs")),
                        str(flatc), str(output)], check=True, cwd=ROOT)
    return output / "catalog.mjs"


def export(args):
    if not re.fullmatch(r"[a-z][a-z0-9_-]*", args.product):
        raise ValueError("Product name must be a lowercase English identifier")
    args.project, args.builder = plain_path(args.project), plain_path(args.builder)
    runtime = plain_path(args.runtime_dir)
    info = json.loads(safe_file(runtime, "web-build.json").read_text(encoding="utf-8"))
    expected = {"platform": "web", "arch": "wasm32", "product": args.product,
                "configuration": args.configuration, "scriptBackend": "quickjs",
                "testSupport": False}
    if not isinstance(info, dict) or any(info.get(key) != value for key, value in expected.items()):
        raise ValueError("The runtime build does not match the Web export configuration")
    plugins = info.get("plugins")
    if not isinstance(plugins, list) or not {"runtime/resource_graph", "runtime/script"} <= set(plugins):
        raise ValueError("The Web runtime requires resource_graph and script plugins")
    if any(not isinstance(p, str) or not p.startswith("runtime/") or p == "runtime/mcp"
           for p in plugins):
        raise ValueError("The Web runtime must not contain editor or test endpoint plugins")
    toolchain = json.loads(Path(__file__).with_name("toolchain.json").read_text())
    if info.get("emscripten") != toolchain["emscripten"]:
        raise ValueError("The Web runtime does not use the pinned Emscripten toolchain")
    output = validate_output(args.output, ROOT / "output/web/wasm32" / args.configuration / "runtime",
                             [args.project, args.builder, runtime])
    # 固定 SDK 当前把 worker 引导放在 engine.mjs 内；仅部署该构建声明的旁文件。
    files = info.get("files")
    if not isinstance(files, list) or not {"engine.mjs", "engine.wasm"} <= set(files):
        raise ValueError("The runtime build does not declare its module and Wasm files")
    for name in files:
        if not isinstance(name, str) or "/" in name or not re.fullmatch(r"engine[.a-z0-9_-]+", name):
            raise ValueError("The runtime build declares an invalid support file")
        path = safe_file(runtime, name)
        with path.open("rb") as stream:
            checksum = hashlib.file_digest(stream, "sha256").hexdigest()
        if info.get("checksums", {}).get(name) != checksum:
            raise ValueError("The runtime file does not match its successful link receipt: " + name)
    accessor = catalog_accessor(plain_path(args.flatc), args.node)
    command = [str(args.builder), "--project", str(args.project), "--package", "--target", "web"]
    if args.manifest:
        command += ["--manifest", args.manifest.as_posix()]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, encoding="utf-8",
                            check=False, timeout=600)
    if result.returncode:
        raise ValueError("Project resource build failed:\n" + result.stdout + "\n" + result.stderr)
    source = plain_path(parse_build_output(result.stdout, "web"))
    validate_output(output, ROOT / "output", [source])
    with staged_output(output) as stage:
        for name in files:
            shutil.copy2(safe_file(runtime, name), stage / name)
        shutil.copy2(Path(__file__).with_name("bootstrap.mjs"), stage / "bootstrap.mjs")
        shutil.copy2(Path(__file__).parent / "templates/index.html", stage / "index.html")
        (stage / "storage").mkdir()
        shutil.copy2(Path(__file__).parent / "storage/storage.mjs", stage / "storage/storage.mjs")
        shutil.copy2(accessor, stage / "catalog.mjs")
        (stage / "application.mjs").write_text(
            "export const applicationName = " + json.dumps(args.product) + ";\n", encoding="utf-8")
        license_file = Path(__file__).parent / "node_modules/flatbuffers/LICENSE"
        shutil.copy2(license_file, stage / "flatbuffers-license.txt")
        licenses = stage / "licenses"
        licenses.mkdir()
        for copyright_file in plain_path(args.dependency_licenses).glob("*/copyright"):
            shutil.copy2(copyright_file, licenses / (copyright_file.parent.name + ".txt"))
        repository = Path(__file__).resolve().parents[2]
        shutil.copytree(repository / "third_party/notices", licenses / "engine")
        license_sources = ["third_party/ozz-animation/LICENSE.md",
                           "third_party/emscripten/LICENSE",
                           "third_party/emscripten/emdawnwebgpu-license.txt"]
        if "runtime/effects" in plugins:
            license_sources += ["third_party/effekseer/LICENSE", "third_party/efkbgfx/LICENSE"]
        if "runtime/terrain" in plugins:
            license_sources += ["third_party/transvoxel/LICENSE"]
        if "runtime/destruction" in plugins:
            license_sources += ["third_party/blast/PACKAGE-LICENSES/blast-sdk-LICENSE.md"]
        for relative in license_sources:
            shutil.copy2(repository / relative, licenses / relative.replace("/", "-"))
        resources = stage / "assets"
        resources.mkdir()
        receipt = copy_resource_snapshot(source, resources, "web", carrier_roots=("gen", "packs"),
                                         require_unchanged_catalog=True)
        export_runtime_settings(args.project, stage, "web")
        (stage / "web-build.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": "ok", "targetPlatform": "web", "output": str(output),
                      "resourceCount": receipt["resource_count"], "revision": receipt["revision"]}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--builder", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--flatc", type=Path, required=True)
    parser.add_argument("--node", default="node")
    parser.add_argument("--product", required=True)
    parser.add_argument("--configuration", choices=("debug", "release"), required=True)
    parser.add_argument("--manifest", type=Path, default=Path("settings/editor-resource-build.toml"))
    parser.add_argument("--dependency-licenses", type=Path,
                        default=ROOT / "build/dependencies/web/wasm32-emscripten/share")
    try:
        export(parser.parse_args())
        return 0
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print(f"Web export did not complete: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
