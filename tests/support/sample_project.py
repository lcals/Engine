"""从统一示例项目准备临时最小夹具，不引入模型导入器或建模插件依赖。"""

import json
from pathlib import Path
import shutil

from .typescript_toolchain import copy_typescript_toolchain


def prepare_minimal_project(root, project):
    root = Path(root).resolve()
    project = Path(project)
    source = root / "projects/showcase"
    compiler = source / "settings/typescript/node_modules/typescript"
    if not compiler.is_dir():
        raise AssertionError("Run npm ci --prefix projects/showcase/settings/typescript first")
    project.mkdir(parents=True)
    for directory in ("assets", "settings", "cache"):
        (project / directory).mkdir()

    resources = (
        "materials/cube.material",
        "meshes/cube.mesh",
        "scenes/main.scene",
        "prefabs/scenes/main/environment.prefab",
        "shaders/pbr.frag.sc",
        "shaders/pbr.shader",
        "shaders/pbr.surface.sc",
        "shaders/pbr.varying.sc",
        "shaders/pbr.vert.sc",
        "shaders/pbr_helpers.sh",
        "shaders/pbr_surface.sh",
        "shaders/pbr_lighting.sh",
        "shaders/dfg_approx.sh",
        "shaders/material_helpers.sh",
        "shaders/shadow_helpers.sh",
        "shaders/csm_helpers.sh",
        "shaders/local_shadow_helpers.sh",
        "shaders/cluster/lighting.sh",
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
    for filename in ("editor-import.toml", "editor-settings.toml", "runtime-game.toml",
                     "runtime-engine.toml", "runtime-parallel.toml"):
        shutil.copy2(source / "settings" / filename, project / "settings" / filename)
    game_settings = project / "settings/runtime-game.toml"
    game_settings.write_text('[common]\ngame_language = "en"\nstartup_movie = ""\n', encoding="utf-8")
    (project / "settings/typescript").mkdir()
    for filename in ("package.json", "package-lock.json", "tsconfig.json"):
        shutil.copy2(source / "settings/typescript" / filename,
                     project / "settings/typescript" / filename)
    configuration = project / "settings/typescript/tsconfig.json"
    value = json.loads(configuration.read_text(encoding="utf-8"))
    value["files"] = [str(root / "include/engine.d.ts")]
    configuration.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    copy_typescript_toolchain(compiler, project / "settings/typescript/node_modules")
    (project / "settings/editor-resource-build.toml").write_text(
        '[[common.items]]\nname = "game"\nstorage = "package"\n'
        'roots = ["scripts/game.ts", "scenes/main.scene"]\nkeep = []\n',
        encoding="utf-8",
    )
    return project
