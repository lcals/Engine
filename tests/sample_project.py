"""从统一示例项目准备临时最小夹具，不引入模型导入器或建模插件依赖。"""

import json
from pathlib import Path
import shutil


def prepare_minimal_project(root, project):
    root = Path(root).resolve()
    project = Path(project)
    source = root / "projects/showcase"
    compiler = source / "settings/node_modules/typescript"
    if not compiler.is_dir():
        raise AssertionError("Run npm ci --prefix projects/showcase/settings first")
    project.mkdir(parents=True)
    for directory in ("assets", "settings", "cache"):
        (project / directory).mkdir()

    resources = (
        "materials/cube.material",
        "meshes/cube.mesh",
        "scenes/main.scene",
        "shaders/pbr.frag.sc",
        "shaders/pbr.shader",
        "shaders/pbr.varying.sc",
        "shaders/pbr.vert.sc",
        "shaders/pbr_helpers.sh",
        "textures/checker.png",
    )
    for resource in resources:
        for suffix in ("", ".meta"):
            destination = project / "assets" / (resource + suffix)
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source / "assets" / (resource + suffix), destination)
    (project / "assets/scripts").mkdir()
    for suffix in ("", ".meta"):
        shutil.copy2(
            source / ("assets/scripts/modules/minimal.ts" + suffix),
            project / ("assets/scripts/game.ts" + suffix),
        )
    for filename in ("import.toml", "package.json", "package-lock.json", "tsconfig.json"):
        shutil.copy2(source / "settings" / filename, project / "settings" / filename)
    configuration = project / "settings/tsconfig.json"
    value = json.loads(configuration.read_text(encoding="utf-8"))
    value["files"] = [str(root / "include/engine.d.ts")]
    configuration.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    shutil.copytree(compiler, project / "settings/node_modules/typescript")
    for filename, platform, output in (
        ("resource-build.toml", "", "cache/build/minimal"),
        ("resource-build-android.toml", "android", "cache/build/minimal-android"),
    ):
        (project / "settings" / filename).write_text(
            f'target_platform = "{platform}"\noutput = "{output}"\n\n'
            '[[items]]\nname = "game"\nstorage = "package"\n'
            'roots = ["project/scripts/game.ts", "project/scenes/main.scene"]\nkeep = []\n',
            encoding="utf-8",
        )
    return project
