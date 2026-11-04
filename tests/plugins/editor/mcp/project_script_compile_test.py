"""真实 TypeScript 单段编译，检查类型诊断及与既有 ESM namespace 的互操作。"""

import argparse
import json
from pathlib import Path
import sys
import subprocess
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from support.sample_project import prepare_minimal_project


def test(root, temporary):
    project = prepare_minimal_project(root, temporary / "project 中文 $literal")
    state = "export default class Counter { value = 3; }\nexport const counter = new Counter();\n"
    (project / "assets/scripts/state.ts").write_text(state, encoding="utf-8")
    namespace = temporary / "state.mjs"
    namespace.write_text(state, encoding="utf-8")
    request = temporary / "snippet request.json"
    builder = root / "scripts/build_scripts.mjs"
    before = {p.relative_to(project): p.read_bytes() for p in project.rglob("*") if p.is_file()}

    def compile(source, source_path="scripts/mcp-check.ts", error=None):
        request.write_text(
            json.dumps({"sourcePath": source_path, "source": source}), encoding="utf-8"
        )
        result = subprocess.run(
            ["node", str(builder), "--project", str(project), "--snippet", str(request)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=30,
        )
        if error:
            assert result.returncode != 0 and error in result.stderr, result.stderr
            assert not result.stdout, "failed compile must not publish JavaScript"
            return None
        assert result.returncode == 0, result.stderr
        output = json.loads(result.stdout)
        assert output["modulePath"] == source_path[:-3] + ".js"
        assert len(output["javascript"].encode("utf-8")) <= 256 * 1024
        return output

    source = """import Counter, { counter } from "./state.js";
export default function(args: { increment: number }, gameplay: { calls: number }) {
    counter.value += args.increment;
    gameplay.calls++;
    return { value: counter.value, calls: gameplay.calls, sameType: counter instanceof Counter };
}"""
    compiled = temporary / "compiled.json"
    compiled.write_text(json.dumps(compile(source)), encoding="utf-8")
    runner = temporary / "run.mjs"
    runner.write_text(
        """import fs from "node:fs";
import vm from "node:vm";
import * as state from "./state.mjs";
const { javascript } = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
const gameplay = { calls: 0 };
const results = [];
for (let i = 0; i < 2; i++) {
    const module = { exports: {} };
    vm.compileFunction(javascript, ["exports", "module", "require"])(
        module.exports, module, (name) => {
            if (name !== "./state.js") throw new Error("Unexpected module: " + name);
            return state;
        });
    results.push(module.exports.default({ increment: 2 }, gameplay));
}
process.stdout.write(JSON.stringify(results));
""",
        encoding="utf-8",
    )
    ran = subprocess.run(
        ["node", str(runner), str(compiled)],
        capture_output=True,
        text=True,
        encoding="utf-8",
        timeout=10,
    )
    assert ran.returncode == 0, ran.stderr
    assert json.loads(ran.stdout) == [
        {"value": 5, "calls": 1, "sameType": True},
        {"value": 7, "calls": 2, "sameType": True},
    ], ran.stdout
    for imports in (
        'import * as state from "./state.js";',
        'declare function require(name: string): any; const state = require("./state.js");',
    ):
        compiled.write_text(
            json.dumps(compile(imports + """
export default function(args: { increment: number }, gameplay: { calls: number }) {
    state.counter.value += args.increment;
    gameplay.calls++;
    return { value: state.counter.value, calls: gameplay.calls,
        sameType: state.counter instanceof state.default };
}""")),
            encoding="utf-8",
        )
        ran = subprocess.run(
            ["node", str(runner), str(compiled)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            timeout=10,
        )
        assert ran.returncode == 0, ran.stderr
        assert json.loads(ran.stdout) == [
            {"value": 5, "calls": 1, "sameType": True},
            {"value": 7, "calls": 2, "sameType": True},
        ], ran.stdout
    other = compile('export default function() { return "new source"; }')
    assert "new source" in other["javascript"] and "Counter" not in other["javascript"]
    compile('import { Scene } from "engine"; export default function() { return Scene !== null; }')
    compile(
        'import { counter } from "./state"; export default function() { return counter.value; }',
        error="explicit .js path",
    )
    compile(
        'import type Counter from "./state"; export default function(args: Counter) { return args.value; }'
    )
    compile(
        'export default function() { const value: number = "bad"; return value; }', error="TS2322"
    )
    compile('export default function() { return import("./state.js"); }', error="Dynamic import")
    compile(
        'declare function require(name: string): any; const name = "./state.js"; '
        "export default function() { return require(name); }",
        error="static module path",
    )
    compile("await Promise.resolve(); export default function() { return null; }", error="TS1378")
    for name in (
        "../escape.ts",
        "/absolute.ts",
        "scripts/../escape.ts",
        "scripts\\escape.ts",
        "x.d.ts",
    ):
        compile(
            "export default function() { return null; }", source_path=name, error="assets-relative"
        )
    compile("//" + "x" * (256 * 1024), error="exceeds 256 KiB")
    after = {p.relative_to(project): p.read_bytes() for p in project.rglob("*") if p.is_file()}
    assert before == after, "snippet compilation must not change source, meta, catalog or cache"
    print(
        "PASS TypeScript diagnostics, virtual paths, ESM namespace/default imports, shared state and source isolation"
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="engine-project-script-compile-") as directory:
        test(args.root.resolve(), Path(directory))
