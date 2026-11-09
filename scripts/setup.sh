#!/usr/bin/env bash
# 宿主构建工具准备：默认只检查，显式 --install 才调用系统包管理器。
set -o pipefail

usage()
{
    cat <<'EOF'
Usage: bash scripts/setup.sh [--check | --install] [--cache ccache|sccache|none]
Check CMake >= 3.30, Git, Ninja and the selected compiler cache; macOS/Linux default to ccache.
Install from Homebrew on macOS or the host apt-get, dnf or pacman repositories on Linux.
Prepare compilers, platform SDKs and vcpkg as described in README.
Exit codes: 0 ready; 1 missing tools, installation or version failure; 2 unsupported arguments or host.
EOF
}

mode=check
mode_selected=false
cache_tool=ccache
while (($# > 0)); do
    case "$1" in
        --check|--install)
            if $mode_selected && [[ "$mode" != "${1#--}" ]]; then
                printf '%s\n' '--check and --install cannot be combined.' >&2
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
        *) printf 'Unknown argument: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

platform=$(uname -s)
case "$platform" in
    Darwin|Linux) ;;
    *) printf 'This entry point does not support host %s; on Windows run scripts/setup.ps1.\n' "$platform" >&2
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
            printf 'Missing or not executable: %s\n' "$tool" >&2
            missing+=("$tool")
            continue
        fi
        if [[ "$tool" == cmake ]]; then
            if [[ "$output" =~ ^cmake[[:space:]]version[[:space:]]([0-9]+)\.([0-9]+) ]]; then
                major=${BASH_REMATCH[1]}
                minor=${BASH_REMATCH[2]}
                if ((major < 3 || (major == 3 && minor < 30))); then
                    printf 'Version too old: %s; CMake >= 3.30 is required.\n' "${output%%$'\n'*}" >&2
                    missing+=("$tool")
                    continue
                fi
            else
                printf 'Cannot identify CMake version: %s\n' "$executable" >&2
                missing+=("$tool")
                continue
            fi
        fi
        printf 'Available: %s [%s]\n' "${output%%$'\n'*}" "$executable"
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
        printf '%s\n' 'Installation permission is unavailable: ask an administrator to install the tools, or prepare sudo and retry.' >&2
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
            printf '%s\n' 'Homebrew is missing. Install it using the official instructions at https://brew.sh/ and retry.' >&2
            return 2
        fi
        if ! prefix=$("$brew_path" --prefix); then
            printf '%s\n' 'Cannot read the Homebrew installation path; repair Homebrew and retry.' >&2
            return 1
        fi
        # 本进程识别现有 Homebrew 工具，避免 PATH 未刷新时重复安装。
        export PATH="$prefix/bin:$PATH"
        if inspect_tools; then
            printf 'This script uses %s/bin; ensure the calling terminal PATH also contains it.\n' "$prefix"
            return 0
        fi
        for tool in "${missing[@]}"; do
            if "$brew_path" list --versions "$tool" >/dev/null 2>&1; then
                "$brew_path" upgrade "$tool" || return 1
            else
                "$brew_path" install "$tool" || return 1
            fi
        done
        printf 'Ensure the calling terminal PATH contains %s/bin.\n' "$prefix"
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
        printf '%s\n' 'No supported package manager found (apt-get, dnf, pacman). Install the tools manually and run --check.' >&2
        return 2
    fi
}

if inspect_tools; then
    printf '%s\n' 'Host build tool checks passed.'
    exit 0
fi
if [[ "$mode" == check ]]; then
    printf '%s\n' 'Run bash scripts/setup.sh --install with the desired --cache option, then check again.' >&2
    exit 1
fi
if install_tools; then
    hash -r
else
    status=$?
    printf '%s\n' 'Tool installation did not complete. Inspect package manager errors, network and permissions before retrying.' >&2
    exit "$status"
fi
if ! inspect_tools; then
    printf '%s\n' 'Post-install checks failed; inspect PATH and repository versions, then prepare CMake >= 3.30 and retry.' >&2
    exit 1
fi
printf '%s\n' 'Host build tool installation and checks completed.'
