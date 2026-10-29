"""Exercise generated FlatBuffers C++ -> TypeScript -> C++ data without production file APIs."""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def run(command):
    result = subprocess.run(command, text=True, capture_output=True, timeout=60)
    if result.returncode:
        raise RuntimeError(
            f"Command failed ({result.returncode}): {command}\n{result.stdout}\n{result.stderr}"
        )
    if result.stdout:
        print(result.stdout.strip())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--generated", type=Path, required=True)
    parser.add_argument("--settings", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--node", required=True)
    args = parser.parse_args()
    modules = args.settings.resolve() / "node_modules"
    runtime = modules / "flatbuffers" / "package.json"
    compiler = modules / "typescript" / "lib" / "tsc.js"
    if not runtime.is_file() or not compiler.is_file():
        raise RuntimeError(
            "Install the fixed project tooling with npm ci in project settings first"
        )
    if json.loads(runtime.read_text(encoding="utf-8"))["version"] != "25.9.23":
        raise RuntimeError("This schema bridge requires the pinned flatbuffers@25.9.23 runtime")
    generated = args.generated.resolve()
    if not (generated / "engine" / "schema.ts").is_file():
        raise RuntimeError("Build engine_scene_schema before running the cross-language test")
    args.work_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="scene-schema-", dir=args.work_dir) as temporary:
        workspace = Path(temporary).resolve()
        source = workspace / "src"
        output = workspace / "javascript"
        shutil.copytree(generated / "engine", source / "engine")
        shutil.copyfile(Path(__file__).with_name("scene_schema_bridge.ts"), source / "bridge.ts")
        run(
            [
                args.node,
                str(compiler),
                "--target",
                "ES2020",
                "--module",
                "commonjs",
                "--moduleResolution",
                "node",
                "--strict",
                "--skipLibCheck",
                "--baseUrl",
                str(modules),
                "--rootDir",
                str(source),
                "--outDir",
                str(output),
                str(source / "bridge.ts"),
            ]
        )
        original = workspace / "cpp.scene"
        changed = workspace / "typescript.scene"
        run([str(args.binary.resolve()), "--write-buffer", str(original)])
        # 本次测试独占固定版本运行库，避免 NODE_PATH 或其他项目依赖改变解析结果。
        shutil.copytree(modules / "flatbuffers", output / "node_modules" / "flatbuffers")
        (output / "package.json").write_text('{"type":"commonjs"}\n', encoding="utf-8")
        run([args.node, str(output / "bridge.js"), str(original), str(changed)])
        run([str(args.binary.resolve()), "--read-buffer", str(changed), str(original)])
    print("Generated C++/TypeScript schema, all component payloads, edits and 64-bit IDs passed.")


if __name__ == "__main__":
    main()
