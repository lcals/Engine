#!/usr/bin/env python3
"""为从模板创建的新仓库设置项目名，无第三方依赖。"""

import argparse
from pathlib import Path
import re


# C++20 保留字、标准命名空间及 Windows 保留目录名不可用作项目标识。
RESERVED_NAMES = frozenset("""
alignas alignof and and_eq asm atomic_cancel atomic_commit atomic_noexcept auto
bitand bitor bool break case catch char char8_t char16_t char32_t class compl
concept const consteval constexpr constinit const_cast continue co_await co_return
co_yield decltype default delete do double dynamic_cast else enum explicit export
extern false float for friend goto if import inline int long module mutable
namespace new noexcept not not_eq nullptr operator or or_eq private protected public
register reinterpret_cast requires return short signed sizeof static static_assert
static_cast struct switch synchronized template this thread_local throw true try
typedef typeid typename union unsigned using virtual void volatile wchar_t while xor
xor_eq std con prn aux nul
""".split()) | {f"{prefix}{number}" for prefix in ("com", "lpt") for number in range(1, 10)}


def initialize_project(root: Path, name: str) -> None:
    """校验项目名后更新项目标识，并重命名公开头文件目录。"""
    if not re.fullmatch(r"[a-z][a-z0-9]*(?:-[a-z0-9]+)*", name):
        raise ValueError("项目名须以小写字母开头，只能包含小写字母、数字和单个连字符。")

    identifier = name.replace("-", "_")
    if identifier in RESERVED_NAMES:
        raise ValueError("项目名不能使用 C++ 保留字、std 或 Windows 保留目录名。")

    root = root.resolve()
    template_include = root / "include" / "cpp_template"
    if not template_include.is_dir() or not (root / "CMakeLists.txt").is_file():
        raise ValueError("未找到模板文件；请在完整模板中运行，且只初始化一次。")

    destination = template_include.with_name(identifier)
    if destination != template_include and destination.exists():
        raise ValueError(f"目标头文件目录已存在：{destination}")

    replacements = (
        ("CPP_TEMPLATE", identifier.upper()),
        ("cpp_template", identifier),
        ("cpp-template", name),
    )
    files = [root / path for path in ("CMakeLists.txt", "CMakePresets.json", "vcpkg.json", "README.md", "AGENTS.md")]
    for directory in ("cmake", "docs", "include", "src", "tests", "third_party", ".github"):
        files.extend(path for path in (root / directory).rglob("*") if path.is_file())

    # 先完成全部读取，避免编码或文件读取错误导致只更新部分文件。
    updates = []
    for path in files:
        if not path.is_file():
            continue
        original = path.read_text(encoding="utf-8")
        updated = original
        for old, new in replacements:
            updated = updated.replace(old, new)
        if updated != original:
            updates.append((path, updated))

    for path, content in updates:
        path.write_text(content, encoding="utf-8", newline="\n")
    if destination != template_include:
        template_include.rename(destination)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("name", help="新项目名，例如 my-app")
    arguments = parser.parse_args()
    try:
        initialize_project(Path(__file__).resolve().parent.parent, arguments.name)
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(1, f"初始化失败：{error}\n")
    print(f"已初始化项目 {arguments.name}，可执行 cmake --workflow --preset debug。")


if __name__ == "__main__":
    main()
