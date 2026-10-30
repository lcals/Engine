"""验证离开源码树的脚本工具：声明入口、固定编译器和项目声明不能丢失。"""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def test(root, node, temporary):
    tools = temporary / "portable tools"
    (tools / "scripts").mkdir(parents=True)
    (tools / "include").mkdir()
    shutil.copy2(root / "scripts/build_scripts.mjs", tools / "scripts/build_scripts.mjs")
    shutil.copy2(root / "include/engine.d.ts", tools / "include/engine.d.ts")
    compiler = root / "projects/showcase/settings/node_modules/typescript"
    shutil.copytree(compiler, tools / "node_modules/typescript")
    project = temporary / "moved project"
    for directory in ["settings", "assets", "cache"]:
        (project / directory).mkdir(parents=True)
    version = json.loads((compiler / "package.json").read_text(encoding="utf-8"))["version"]
    package = project / "settings/package.json"
    package.write_text(json.dumps({"devDependencies": {"typescript": version}}), encoding="utf-8")
    (project / "settings/tsconfig.json").write_text(
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
                "include": ["../assets/**/*.ts"],
            }
        ),
        encoding="utf-8",
    )
    (project / "assets/engine.d.ts").write_text(
        "declare const ownDeclaration: number;\n", encoding="utf-8"
    )
    (project / "assets/main.ts").write_text(
        'import { Debug } from "engine";\nDebug.Log(String(ownDeclaration));\n',
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
    assert (tools / "node_modules/typescript/lib/typescript.js").as_posix() in paths
    result = run("--output", str(project / "cache/staging"))
    assert result.returncode == 0, result.stderr
    assert (project / "cache/staging/main.js").is_file()
    package.write_text('{"devDependencies":{"typescript":"0.0.0"}}', encoding="utf-8")
    result = run("--inputs")
    assert result.returncode != 0 and "Project requires TypeScript 0.0.0" in result.stderr
    # 已安装的项目编译器继续优先，不能被随包版本覆盖。
    shutil.copytree(compiler, project / "settings/node_modules/typescript")
    result = run("--inputs")
    assert result.returncode == 0, result.stderr
    paths = {entry["path"] for entry in json.loads(result.stdout)["inputs"]}
    assert (project / "settings/node_modules/typescript/lib/typescript.js").as_posix() in paths
    assert (tools / "node_modules/typescript/lib/typescript.js").as_posix() not in paths


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
