#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_dir/build"
clean_build=0
profile=""

usage() { printf 'Usage: %s [--clean] [--profile full|minimal]\n' "$0"; }
while [ "$#" -gt 0 ]; do
    case "$1" in
        --clean) clean_build=1; shift ;;
        --profile)
            case "${2:-}" in
                full|minimal) profile="$2"; shift 2 ;;
                *) usage >&2; exit 2 ;;
            esac ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done
if [ -n "$profile" ]; then
    build_dir="$repo_dir/build-$profile"
    if [ -L "$build_dir" ]; then
        printf 'Refusing to reset config in symlinked profile directory: %s\n' "$build_dir" >&2
        exit 1
    fi
fi
if ! command -v idf.py >/dev/null 2>&1 || [ -z "${IDF_PATH:-}" ]; then
    printf 'ESP-IDF is not active. Run: source tools/activate-dev-env.sh\n' >&2
    exit 1
fi

export IDF_TARGET="${ZECTRIX_IDF_TARGET:-esp32s3}"
export IDF_SKIP_CHECK_SUBMODULES="${IDF_SKIP_CHECK_SUBMODULES:-1}"

cd "$repo_dir"
idf_args=(--ccache -B "$build_dir")
if [ "$clean_build" -eq 1 ]; then
    if [ -d "$build_dir" ] && [ ! -f "$build_dir/CMakeCache.txt" ]; then
        if [ -L "$build_dir" ]; then
            printf 'Refusing to clean symlinked build directory: %s\n' "$build_dir" >&2
            exit 1
        fi
        cmake -E remove_directory "$build_dir"
    fi
    idf.py "${idf_args[@]}" fullclean
fi
if [ -n "$profile" ]; then
    mkdir -p "$build_dir"
    # Named profiles always start from committed defaults, never saved menuconfig.
    # The ordinary build and the developer's root sdkconfig are left untouched.
    rm -f "$build_dir/sdkconfig" "$build_dir/sdkconfig.old"
    idf_args+=(-D "SDKCONFIG=$build_dir/sdkconfig"
        -D "SDKCONFIG_DEFAULTS=$repo_dir/sdkconfig.defaults;$repo_dir/tools/profiles/$profile.defaults")
    idf.py "${idf_args[@]}" reconfigure build size --format json --output-file "$build_dir/size.json"
else
    idf.py "${idf_args[@]}" build
fi

actual_target="$(jq -r '.target // .project_target // "unknown"' "$build_dir/project_description.json")"
if [ "$actual_target" != "$IDF_TARGET" ]; then
    printf 'Built wrong target: expected=%s actual=%s\n' "$IDF_TARGET" "$actual_target" >&2
    exit 1
fi

printf 'PASS: firmware build target=%s profile=%s ccache=enabled\n' "$actual_target" "${profile:-local}"
