"""验证离开源码树的脚本工具：声明入口、固定编译器和项目声明不能丢失。"""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from support.typescript_toolchain import copy_typescript_toolchain


def test(root, node, temporary):
    tools = temporary / "portable tools"
    (tools / "scripts").mkdir(parents=True)
    (tools / "include").mkdir()
    shutil.copy2(root / "scripts/build_scripts.mjs", tools / "scripts/build_scripts.mjs")
    shutil.copy2(root / "include/engine.d.ts", tools / "include/engine.d.ts")
    compiler = root / "projects/showcase/settings/typescript/node_modules/typescript"
    copy_typescript_toolchain(compiler, tools / "node_modules")
    project = temporary / "moved project"
    for directory in ["settings/typescript", "assets", "cache"]:
        (project / directory).mkdir(parents=True)
    version = json.loads((compiler / "package.json").read_text(encoding="utf-8"))["version"]
    package = project / "settings/typescript/package.json"
    package.write_text(json.dumps({"devDependencies": {"typescript": version}}), encoding="utf-8")
    (project / "settings/typescript/tsconfig.json").write_text(
        json.dumps(
            {
                "compilerOptions": {
                    "target": "ES2022",
                    "module": "ES2022",
                    "strict": True,
                    "moduleResolution": "Bundler",
                    "types": [],
                    "lib": ["ES2022"],
                },
                "files": [str(temporary / "missing engine/include/engine.d.ts")],
                "include": ["../../assets/**/*.ts"],
            }
        ),
        encoding="utf-8",
    )
    (project / "assets/engine.d.ts").write_text(
        "declare const ownDeclaration: number;\n", encoding="utf-8"
    )
    (project / "assets/main.ts").write_text(
        'import { App, Debug } from "engine";\n'
        "Debug.Log(App.assetsPath + String(ownDeclaration));\n"
        "// @ts-expect-error The removed path alias must not be declared.\n"
        "App.builtinAssetsPath;\n",
        encoding="utf-8",
    )

    def run(*arguments):
        return subprocess.run(
            [
                str(node),
                str(tools / "scripts/build_scripts.mjs"),
                "--project",
                str(project),
                *arguments,
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=30,
        )

    result = run("--inputs")
    assert result.returncode == 0, result.stderr
    paths = {entry["path"] for entry in json.loads(result.stdout)["inputs"]}
    assert (tools / "include/engine.d.ts").as_posix() in paths
    assert (project / "assets/engine.d.ts").as_posix() in paths
    assert (tools / "node_modules/typescript/dist/api/sync/api.js").as_posix() in paths
    native = tools / "node_modules/@typescript"
    assert any(str(filename).replace("\\", "/") in paths for filename in native.glob("*/lib/tsc*"))
    result = run("--output", str(project / "cache/staging"))
    assert result.returncode == 0, result.stderr
    assert (project / "cache/staging/main.js").is_file()
    package.write_text('{"devDependencies":{"typescript":"0.0.0"}}', encoding="utf-8")
    result = run("--inputs")
    assert result.returncode != 0 and "Project requires TypeScript 0.0.0" in result.stderr
    # 已安装的项目编译器继续优先，不能被随包版本覆盖。
    copy_typescript_toolchain(compiler, project / "settings/typescript/node_modules")
    result = run("--inputs")
    assert result.returncode == 0, result.stderr
    paths = {entry["path"] for entry in json.loads(result.stdout)["inputs"]}
    assert (project / "settings/typescript/node_modules/typescript/dist/api/sync/api.js").as_posix() in paths
    assert (tools / "node_modules/typescript/dist/api/sync/api.js").as_posix() not in paths
    # 片段在 cache 中编译，沿用实际 sourcePath 相对导入，但不能污染 assets。
    (project / "assets/dependency.ts").write_text("export const value = 7;\n", encoding="utf-8")
    request = project / "cache/request.json"

    def snippet(source, source_path="scripts/review.ts"):
        request.write_text(
            json.dumps({"sourcePath": source_path, "source": source}), encoding="utf-8"
        )
        return run("--snippet", str(request))

    result = snippet(
        'import { App } from "engine";\n'
        'import { value } from "../dependency.js";\n'
        "export const result = [App.assetsPath, value];\n"
    )
    assert result.returncode == 0, result.stderr
    compiled = json.loads(result.stdout)
    assert compiled["modulePath"] == "scripts/review.js"
    assert 'require("../dependency.js")' in compiled["javascript"]
    assert not (project / "assets/scripts").exists()
    result = snippet('export const value = import("../dependency.js");')
    assert result.returncode != 0 and "Dynamic import is not supported" in result.stderr
    result = snippet('export const value: number = "wrong";')
    assert result.returncode != 0 and "not assignable" in result.stderr
    result = snippet("export const value = 7;", "../outside.ts")
    assert result.returncode != 0 and "assets-relative" in result.stderr
    assert not list((project / "cache").glob(".script-build-*"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--node", default="node")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="engine-editor-tools-") as directory:
        test(args.root.resolve(), args.node, Path(directory).resolve())
    print(
        "Portable TypeScript toolchain, declaration mapping and project compiler precedence passed"
    )
