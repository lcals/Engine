"""检查自有 C++ 包含方向，以及 CMake 导出的真实 engine_* 目标依赖。"""

import argparse
import json
from pathlib import Path
import re

ALLOWED = {
    "runtime": {"runtime"},
    "editor": {"runtime", "editor"},
    "plugins/runtime": {"runtime", "plugins/runtime"},
    "plugins/editor": {"runtime", "editor", "plugins/runtime", "plugins/editor"},
}
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', re.MULTILINE)


def file_scope(path, root):
    """仅以真实文件目录确定作用域；宿主、测试和外部库不冒充核心模块。"""
    path = Path(path)
    if not path.is_absolute():
        path = root / path
    try:
        relative = path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return None
    for prefix in ("src/engine/", "include/engine/"):
        if relative.startswith(prefix):
            suffix = relative[len(prefix) :]
            for scope in reversed(ALLOWED):
                if suffix.startswith(scope + "/"):
                    return scope
    if relative.startswith(("platforms/", "include/schemas/runtime/")):
        return "runtime"
    if relative.startswith(("src/", "tests/")):
        return "host"
    return None


def include_errors(root):
    errors = []
    for directory in (root / "include/engine", root / "src/engine"):
        for path in sorted(directory.rglob("*")):
            if path.suffix not in {".h", ".hpp", ".cpp", ".mm", ".c"}:
                continue
            owner = file_scope(path, root)
            if owner not in ALLOWED:
                continue
            content = path.read_text(encoding="utf-8")
            for match in INCLUDE.finditer(content):
                included = match[1]
                candidates = (
                    path.parent / included,
                    root / "include/engine" / included,
                    root / "src/engine" / included,
                )
                scopes = {
                    file_scope(candidate, root) for candidate in candidates if candidate.is_file()
                }
                # 即使禁用插件未检出其头，目录契约仍必须检查。
                logical = included.removeprefix("engine/")
                scopes.update(scope for scope in ALLOWED if logical.startswith(scope + "/"))
                for dependency in sorted(scopes - {None, "host"}):
                    if dependency not in ALLOWED[owner]:
                        line = content.count("\n", 0, match.start()) + 1
                        errors.append(
                            f"{path.relative_to(root)}:{line}: "
                            f"{owner} includes {dependency}: {included}"
                        )
    return errors


def target_errors(targets, root):
    errors = []
    by_name = {target["name"]: target for target in targets}
    aliases = {}
    for target in targets:
        aliases.update(target.get("aliases", {}))
    scopes = {}
    edges = {}
    for target in targets:
        name = target["name"]
        directory = Path(target["source_directory"])
        actual = {file_scope(directory / source, root) for source in target["sources"]}
        actual.discard(None)
        if "host" in actual:
            scopes[name] = "host"
        elif len(actual) > 1:
            errors.append(f"{name}: mixes source scopes: {', '.join(sorted(actual))}")
            scopes[name] = None
        else:
            scopes[name] = next(iter(actual), None)
        links = target["links"] + target["interface_links"]
        if any("$<" in value for value in target["sources"] + links):
            errors.append(f"{name}: unevaluated CMake generator expression")
        edges[name] = {aliases.get(link, link) for link in links} & by_name.keys()
        for link in links:
            if link.startswith(("engine_", "engine::")) and aliases.get(link, link) not in by_name:
                errors.append(f"{name}: missing exported engine target: {link}")

    # 无源文件的接口目标作为透明连接；不能借它绕过模块方向检查。
    for origin, owner in scopes.items():
        if owner not in ALLOWED:
            continue
        pending = [(dependency, [origin, dependency]) for dependency in sorted(edges[origin])]
        visited = {origin}
        while pending:
            dependency, route = pending.pop()
            if dependency in visited:
                continue
            visited.add(dependency)
            scope = scopes[dependency]
            if scope is not None and scope not in ALLOWED[owner]:
                errors.append(
                    f"forbidden target dependency: {' -> '.join(route)} ({owner} -> {scope})"
                )
            pending.extend((child, route + [child]) for child in sorted(edges[dependency]))

    active = []
    completed = set()

    def visit(name):
        if name in active:
            cycle = active[active.index(name) :] + [name]
            errors.append("target dependency cycle: " + " -> ".join(cycle))
            return
        if name in completed:
            return
        active.append(name)
        for child in sorted(edges[name]):
            visit(child)
        active.pop()
        completed.add(name)

    for name in sorted(edges):
        visit(name)
    return errors


def read_targets(directory, configuration):
    targets = json.loads((directory / "targets.json").read_text(encoding="utf-8"))
    for target in targets:
        for property_name in ("sources", "links", "interface_links"):
            path = directory / configuration / f'{target["name"]}.{property_name}'
            target[property_name] = [
                line for line in path.read_text(encoding="utf-8").splitlines() if line
            ]
    return targets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--targets", type=Path, required=True)
    parser.add_argument("--config", default="")
    args = parser.parse_args()
    root = args.root.resolve()
    errors = include_errors(root) + target_errors(read_targets(args.targets, args.config), root)
    for error in errors:
        print(error)
    if errors:
        return 1
    print("Architecture checks passed: source includes and resolved CMake target dependencies")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
