#!/usr/bin/env bash
# 宿主构建工具准备：默认只检查，显式 --install 才调用系统包管理器。
set -o pipefail

usage()
{
    cat <<'EOF'
用法：bash scripts/setup.sh [--check | --install] [--cache ccache|sccache|none]
检查 CMake >= 3.25、Git、Ninja 和所选编译缓存；macOS/Linux 默认 ccache。
安装源：macOS Homebrew；Linux 当前系统的 apt-get、dnf 或 pacman 仓库。
编译器、平台 SDK 和 vcpkg 仍按 README 准备。
退出码：0 工具可用；1 缺失、安装或版本检查失败；2 参数或宿主环境不支持。
EOF
}

mode=check
mode_selected=false
cache_tool=ccache
while (($# > 0)); do
    case "$1" in
        --check|--install)
            if $mode_selected && [[ "$mode" != "${1#--}" ]]; then
                printf '%s\n' '--check 与 --install 不能同时使用。' >&2
                exit 2
            fi
            mode=${1#--}
            mode_selected=true
            ;;
        --cache)
            if (($# < 2)); then
                usage >&2
                exit 2
            fi
            cache_tool=$2
            shift
            case "$cache_tool" in ccache|sccache|none) ;; *) usage >&2; exit 2 ;; esac
            ;;
        --help|-h) usage; exit 0 ;;
        *) printf '未知参数：%s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

platform=$(uname -s)
case "$platform" in
    Darwin|Linux) ;;
    *) printf '当前宿主 %s 不支持此入口；Windows 请运行 scripts/setup.ps1。\n' "$platform" >&2
       exit 2 ;;
esac

tools=(cmake git ninja)
if [[ "$cache_tool" != none ]]; then
    tools+=("$cache_tool")
fi

inspect_tools()
{
    missing=()
    local tool executable output major minor
    for tool in "${tools[@]}"; do
        executable=$(command -v "$tool")
        if [[ -z "$executable" ]] || ! output=$("$executable" --version 2>&1); then
            printf '缺失或不可执行：%s\n' "$tool" >&2
            missing+=("$tool")
            continue
        fi
        if [[ "$tool" == cmake ]]; then
            if [[ "$output" =~ ^cmake[[:space:]]version[[:space:]]([0-9]+)\.([0-9]+) ]]; then
                major=${BASH_REMATCH[1]}
                minor=${BASH_REMATCH[2]}
                if ((major < 3 || (major == 3 && minor < 25))); then
                    printf '版本过低：%s；要求 CMake >= 3.25。\n' "${output%%$'\n'*}" >&2
                    missing+=("$tool")
                    continue
                fi
            else
                printf '无法识别 CMake 版本：%s\n' "$executable" >&2
                missing+=("$tool")
                continue
            fi
        fi
        printf '可用：%s [%s]\n' "${output%%$'\n'*}" "$executable"
    done
    ((${#missing[@]} == 0))
}

run_as_root()
{
    if [[ $(id -u) == 0 ]]; then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        printf '%s\n' '缺少安装权限：请由管理员安装所列工具，或准备 sudo 后重试。' >&2
        return 2
    fi
}

install_tools()
{
    local tool package prefix brew_path
    local packages=()
    if [[ "$platform" == Darwin ]]; then
        brew_path=$(command -v brew)
        if [[ -z "$brew_path" ]]; then
            for prefix in /opt/homebrew /usr/local; do
                if [[ -x "$prefix/bin/brew" ]]; then
                    brew_path=$prefix/bin/brew
                    break
                fi
            done
        fi
        if [[ -z "$brew_path" ]]; then
            printf '%s\n' '缺少 Homebrew。请按 https://brew.sh/ 的官方说明安装，再重试。' >&2
            return 2
        fi
        if ! prefix=$("$brew_path" --prefix); then
            printf '%s\n' '无法读取 Homebrew 安装位置，请修复 Homebrew 后重试。' >&2
            return 1
        fi
        # 本进程识别现有 Homebrew 工具，避免 PATH 未刷新时重复安装。
        export PATH="$prefix/bin:$PATH"
        if inspect_tools; then
            printf '当前脚本已使用 %s/bin；请确保调用终端的 PATH 也包含该目录。\n' "$prefix"
            return 0
        fi
        for tool in "${missing[@]}"; do
            if "$brew_path" list --versions "$tool" >/dev/null 2>&1; then
                "$brew_path" upgrade "$tool" || return 1
            else
                "$brew_path" install "$tool" || return 1
            fi
        done
        printf '请确保调用终端的 PATH 包含 %s/bin。\n' "$prefix"
        return 0
    fi

    for tool in "${missing[@]}"; do
        package=$tool
        if [[ "$tool" == ninja ]] &&
           { command -v apt-get >/dev/null 2>&1 || command -v dnf >/dev/null 2>&1; }; then
            package=ninja-build
        fi
        packages+=("$package")
    done
    if command -v apt-get >/dev/null 2>&1; then
        run_as_root apt-get update || return 1
        run_as_root apt-get install -y --no-install-recommends "${packages[@]}"
    elif command -v dnf >/dev/null 2>&1; then
        run_as_root dnf install -y "${packages[@]}"
    elif command -v pacman >/dev/null 2>&1; then
        # 不自动升级整个发行版；管理员负责保持仓库与已安装系统同步。
        run_as_root pacman -S --needed --noconfirm "${packages[@]}"
    else
        printf '%s\n' '未找到支持的包管理器（apt-get、dnf、pacman）。请手动安装工具后运行 --check。' >&2
        return 2
    fi
}

if inspect_tools; then
    printf '%s\n' '宿主构建工具检查通过。'
    exit 0
fi
if [[ "$mode" == check ]]; then
    printf '%s\n' '请先运行 bash scripts/setup.sh --install（保留需要的 --cache 参数），再重新检查。' >&2
    exit 1
fi
if install_tools; then
    hash -r
else
    status=$?
    printf '%s\n' '工具安装未完成。请检查上方包管理器错误、网络或权限后重试。' >&2
    exit "$status"
fi
if ! inspect_tools; then
    printf '%s\n' '安装后检查仍失败；请检查 PATH 和发行版仓库版本，准备 CMake >= 3.25 后重试。' >&2
    exit 1
fi
printf '%s\n' '宿主构建工具安装与检查完成。'
