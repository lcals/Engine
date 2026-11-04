"""Verify real compiler input fingerprints and project-scoped GUID publication."""

import argparse
from contextlib import closing
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import sqlite3
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from support.typescript_toolchain import copy_typescript_toolchain


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def build(builder, project, success=True):
    result = subprocess.run(
        [str(builder), "--project", str(project)],
        capture_output=True,
        text=True,
        encoding="utf-8",
        timeout=90,
        check=False,
    )
    require(
        (result.returncode == 0) is success,
        f"unexpected asset build result {result.returncode}:\n{result.stdout}\n{result.stderr}",
    )
    if not success:
        return None
    match = re.search(
        r"imported=(\d+) reused=(\d+) removed=(\d+) threads=(\d+) revision=([^\s]+)", result.stdout
    )
    require(match is not None, f"missing asset import receipt: {result.stdout}")
    return tuple(int(value) for value in match.groups()[:4]) + (match.group(5),)


def test(builder, source_project, temporary):
    project = temporary / "configuration project 中文"
    for name in ("assets", "settings", "cache"):
        (project / name).mkdir(parents=True)
    (project / "settings/import.toml").write_text("import_threads = 4\n", encoding="utf-8")
    compiler = source_project / "settings/node_modules/typescript"
    require(compiler.is_dir(), "Run npm ci --prefix projects/showcase/settings first")
    copy_typescript_toolchain(compiler, project / "settings/node_modules")
    write_json(project / "settings/package.json", {"private": True, "type": "module"})
    write_json(
        project / "settings/tsconfig.json",
        {
            "extends": "./base.json",
            "include": ["../assets/**/*.ts"],
            "files": ["./custom.d.ts"],
        },
    )
    base = {
        "compilerOptions": {
            "module": "ES2022",
            "moduleResolution": "Bundler",
            "target": "ES2022",
            "strict": True,
            "lib": ["ES2022"],
            "types": [],
            "noUnusedParameters": False,
            "removeComments": False,
        }
    }
    write_json(project / "settings/base.json", base)
    declarations = project / "settings/custom.d.ts"
    declarations.write_text("interface Configuration { label: string; }\n", encoding="utf-8")
    source = project / "assets/game.ts"
    source.write_text(
        "// Kept until inherited removeComments changes.\n"
        "export const configuration: Configuration = { label: 'ready' };\n"
        "export function label(unused: string) { return configuration.label; }\n",
        encoding="utf-8",
    )
    # Legacy directory locks left by an interrupted old JS compiler must not block the OS-locked pipeline.
    (project / "cache/.script-build-lock").mkdir()
    first = build(builder, project)
    require(first[:4] == (1, 0, 0, 4), "first TS import must use the configured four workers")
    require(
        not (project / "cache/assets").exists(), "compiler must not restore legacy asset output"
    )
    require(
        not (project / "settings/custom.d.ts.meta").exists(), "settings declarations are not assets"
    )
    second = build(builder, project)
    require(
        second[:2] == (0, 1) and first[4] == second[4],
        "unchanged compiler input snapshot must hit cache",
    )
    match = re.search(
        r'^guid\s*=\s*"([0-9a-f-]{36})"',
        source.with_name(source.name + ".meta").read_text(encoding="utf-8"),
        re.MULTILINE,
    )
    require(match is not None, "TS source meta was not generated")
    output = project / "cache/gen" / match.group(1)
    original = output.read_bytes()
    catalog = project / "cache/catalog.bin"

    declarations.write_text(
        "interface Configuration { label: string; enabled?: boolean; }\n", encoding="utf-8"
    )
    declared = build(builder, project)
    require(
        declared[:2] == (1, 0) and declared[4] != first[4],
        "settings declaration changes must invalidate the TS task",
    )
    base["compilerOptions"]["removeComments"] = True
    write_json(project / "settings/base.json", base)
    inherited = build(builder, project)
    require(
        inherited[:2] == (1, 0) and output.read_bytes() != original,
        "inherited tsconfig emit settings must change GUID JS",
    )
    valid_output = output.read_bytes()
    valid_catalog = catalog.read_bytes()
    base["compilerOptions"]["noUnusedParameters"] = True
    write_json(project / "settings/base.json", base)
    build(builder, project, success=False)
    require(
        output.read_bytes() == valid_output and catalog.read_bytes() == valid_catalog,
        "new inherited checking rules must run and preserve prior publication on failure",
    )
    base["compilerOptions"]["noUnusedParameters"] = False
    write_json(project / "settings/base.json", base)
    require(build(builder, project)[:2] == (0, 1), "restoring known valid inputs reuses cache")
    declarations.write_text("interface Configuration { label: number; }\n", encoding="utf-8")
    build(builder, project, success=False)
    require(
        output.read_bytes() == valid_output and catalog.read_bytes() == valid_catalog,
        "declaration type errors cannot silently hit cache or publish partial outputs",
    )

    declarations.write_text(
        "interface Configuration { label: string; enabled?: boolean; }\n", encoding="utf-8"
    )
    shader_directory = project / "assets/shaders"
    shutil.copytree(source_project / "assets/shaders", shader_directory)
    include = shader_directory / "lighting.inc"
    include.write_text("#define TEST_TINT 0.8\n", encoding="utf-8")
    fragment = shader_directory / "pbr.frag.sc"
    text = fragment.read_text(encoding="utf-8")
    text = text.replace(
        "#include <bgfx_shader.sh>", '#include <bgfx_shader.sh>\n#include "lighting.inc"'
    )
    tint_target = "    vec3 color = surface.base_color.rgb;"
    require(
        text.count(tint_target) == 1,
        "shader fixture must insert TEST_TINT into the actual sampled base-color calculation",
    )
    text = text.replace(tint_target, "    surface.base_color.rgb *= TEST_TINT;\n" + tint_target)
    fragment.write_text(text, encoding="utf-8")
    shader_assets = {
        path.relative_to(project / "assets").as_posix()
        for path in shader_directory.rglob("*")
        if path.is_file() and not path.name.endswith(".meta")
    }
    shader_sources = sorted(path for path in shader_assets if not path.endswith(".shader"))
    require(
        "shaders/pbr_helpers.sh" in shader_sources,
        "shader fixture must include the shared PBR helper source",
    )
    shader_first = build(builder, project)
    require(
        shader_first[:2] == (len(shader_assets), 1),
        "new shader and every copied/generated source must import while existing TS is reused",
    )
    shader_meta = (shader_directory / "pbr.shader.meta").read_text(encoding="utf-8")
    shader_guid = re.search(r'^guid\s*=\s*"([0-9a-f-]{36})"', shader_meta, re.MULTILINE).group(1)
    shader_output = project / "cache/gen" / shader_guid
    first_shader_bytes = shader_output.read_bytes()
    include.write_text("#define TEST_TINT 0.3\n", encoding="utf-8")
    shader_changed = build(builder, project)
    require(
        shader_changed[:2] == (2, len(shader_assets) - 1)
        and shader_output.read_bytes() != first_shader_bytes,
        "non-.sc/.sh include changes must rebuild only that source and the compiled shader",
    )
    shader_valid = shader_output.read_bytes()
    catalog_valid = catalog.read_bytes()
    include.write_text("#error Intentional include compilation failure\n", encoding="utf-8")
    build(builder, project, success=False)
    require(
        shader_output.read_bytes() == shader_valid and catalog.read_bytes() == catalog_valid,
        "shader include failure must preserve previous GUID artifacts and catalog",
    )

    include.write_text("#define TEST_TINT 0.3\n", encoding="utf-8")
    (project / "assets/helper.ts").write_text("export const helper = 42;\n", encoding="utf-8")
    (project / "assets/types.ts").write_text(
        "export interface Shape { value: number; }\n", encoding="utf-8"
    )
    source.write_text(
        'import { helper } from "./helper.js";\n'
        'import type { Shape } from "./types.js";\n'
        "export const result: Shape = { value: helper };\n",
        encoding="utf-8",
    )
    build(builder, project)
    with closing(sqlite3.connect(project / "cache/assets.sqlite")) as database:
        runtime = database.execute(
            "SELECT dependency.path FROM runtime_dependencies edge "
            "JOIN assets owner ON owner.guid=edge.asset_guid "
            "JOIN assets dependency ON dependency.guid=edge.dependency_guid "
            "WHERE owner.path='game.ts' ORDER BY dependency.path"
        ).fetchall()
        require(
            runtime == [("helper.ts",)],
            "only emitted JS imports become runtime dependencies",
        )
        shader_runtime = database.execute(
            "SELECT dependency_guid FROM runtime_dependencies WHERE asset_guid=? "
            "ORDER BY dependency_guid",
            (shader_guid,),
        ).fetchall()
        require(
            shader_runtime
            == [
                ("00000000-0000-4000-8000-000000000006",),
                ("00000000-0000-4000-8000-000000000007",),
                ("00000000-0000-4000-8000-000000000008",),
            ],
            "shader runtime dependencies must be precisely its three builtin sampler defaults",
        )
        compiled_sources = database.execute(
            "SELECT dependency.path FROM dependencies edge "
            "JOIN assets dependency ON dependency.guid=edge.dependency_guid "
            "WHERE edge.asset_guid=? ORDER BY dependency.path",
            (shader_guid,),
        ).fetchall()
        require(
            compiled_sources == [(path,) for path in shader_sources],
            "shader compilation must track every source include and no TS asset",
        )
    valid_catalog = catalog.read_bytes()
    (project / "assets/helper.ts").write_text(
        'import { result } from "./game.js";\n'
        "export const helper = 42;\n"
        "export function circular() { return result; }\n",
        encoding="utf-8",
    )
    build(builder, project, success=False)
    require(
        catalog.read_bytes() == valid_catalog,
        "runtime dependency cycle cannot publish an unusable catalog",
    )


def test_shader_tool_content(builder, source_project, shaderc, includes, temporary):
    """真实工具即使报告同一版本，内容变化也必须使生产导入和发布状态失效。"""
    root = source_project.parents[1]
    portable = temporary / "isolated builder"
    tools = portable / "tools"
    tools.mkdir(parents=True)
    copied_builder = portable / builder.name
    shutil.copy2(builder, copied_builder)
    copied_shaderc = tools / shaderc.name
    shutil.copy2(shaderc, copied_shaderc)
    # Windows 桌面构建可能启用 app-local DLL；复制当前工具旁的实际依赖。
    for directory, destination in ((builder.parent, portable), (shaderc.parent, tools)):
        for library in directory.glob("*.dll"):
            shutil.copy2(library, destination / library.name)
    shutil.copytree(includes, tools / "bgfx")
    shutil.copytree(root / "include/schemas/runtime", tools / "include/schemas/runtime")
    shutil.copytree(root / "src/engine/runtime/render/shaders", tools / "builtin-shaders")
    project = temporary / "shader tool project"
    for name in ("assets", "settings", "cache"):
        (project / name).mkdir(parents=True)
    shutil.copytree(source_project / "assets/shaders", project / "assets/shaders")
    (project / "settings/import.toml").write_text("import_threads = 2\n", encoding="utf-8")
    asset_count = sum(
        1
        for source in (project / "assets").rglob("*")
        if source.is_file() and not source.name.endswith(".meta")
    )

    def version():
        result = subprocess.run(
            [str(copied_shaderc), "--version"],
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=30,
            check=False,
        )
        require(result.returncode == 0, "isolated shader compiler must remain executable")
        return result.stdout, result.stderr

    def states():
        with closing(sqlite3.connect(project / "cache/assets.sqlite")) as database:
            return dict(
                (row[0], row[1:])
                for row in database.execute(
                    "SELECT path,guid,source_hash,meta_hash,build_key,output_hash FROM assets ORDER BY path"
                )
            )

    original_hash = hashlib.sha256(shaderc.read_bytes()).hexdigest()
    first_version = version()
    first = build(copied_builder, project)
    require(first[:2] == (asset_count, 0), "isolated tool fixture imports all real shader assets")
    require(
        build(copied_builder, project)[:2] == (0, asset_count),
        "unchanged isolated shader compiler must reuse every asset",
    )
    before = states()
    before_hash = hashlib.sha256(copied_shaderc.read_bytes()).hexdigest()
    stamp = copied_shaderc.stat()
    # 不改变真实编译器实现或版本输出。Mach-O 通过新的有效 ad-hoc 签名标识改变
    # 文件内容；不能向签名 Mach-O 追加垃圾或依赖系统放宽校验。PE/ELF 允许末尾负载。
    if sys.platform == "darwin":
        signed = subprocess.run(
            [
                "/usr/bin/codesign",
                "--force",
                "--sign",
                "-",
                "--identifier",
                "engine.shader-fingerprint-regression",
                str(copied_shaderc),
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=30,
        )
        require(signed.returncode == 0, f"cannot sign isolated shader compiler: {signed.stderr}")
    else:
        with copied_shaderc.open("ab") as stream:
            stream.write(b"\nEngine shader tool fingerprint regression payload\n")
    # 工具指纹不能只依赖修改时间。

    os.utime(copied_shaderc, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
    require(
        hashlib.sha256(copied_shaderc.read_bytes()).hexdigest() != before_hash,
        "shader compiler content must actually change",
    )
    require(
        version() == first_version, "changed shader compiler must report exactly the same version"
    )
    rebuilt = build(copied_builder, project)
    require(
        rebuilt[:2] == (1, asset_count - 1),
        "same-version compiler content changes must rebuild only the compiled shader",
    )
    after = states()
    require(before.keys() == after.keys(), "tool replacement cannot change asset identities")
    for source, old in before.items():
        current = after[source]
        if source.endswith(".shader"):
            require(
                old[:3] == current[:3] and old[3] != current[3] and old[4] == current[4],
                "same-version tool updates must publish a new build key with identical source/GUID/output",
            )
        else:
            require(
                old == current, "compiler replacement must not invalidate unrelated source assets"
            )
    require(
        build(copied_builder, project)[:2] == (0, asset_count),
        "the newly published tool fingerprint must be reused on the following import",
    )
    require(
        hashlib.sha256(shaderc.read_bytes()).hexdigest() == original_hash,
        "the installed production compiler must remain untouched",
    )
    print(
        "Same-version real shader compiler replacement invalidates only shader build keys and reuses the new publication"
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--builder", type=Path, required=True)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--shaderc", type=Path, required=True)
    parser.add_argument("--shader-includes", type=Path, required=True)
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="engine-import-inputs-") as directory:
        test(arguments.builder.resolve(), arguments.project.resolve(), Path(directory).resolve())
        test_shader_tool_content(
            arguments.builder.resolve(),
            arguments.project.resolve(),
            arguments.shaderc.resolve(),
            arguments.shader_includes.resolve(),
            Path(directory).resolve(),
        )


if __name__ == "__main__":
    main()
