#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$repo_dir/build"
clean_build=0

if [ "${1:-}" = "--clean" ]; then
    clean_build=1
    shift
fi
if [ "$#" -ne 0 ]; then
    printf 'Usage: %s [--clean]\n' "$0" >&2
    exit 2
fi
if ! command -v idf.py >/dev/null 2>&1 || [ -z "${IDF_PATH:-}" ]; then
    printf 'ESP-IDF is not active. Run: source tools/activate-dev-env.sh\n' >&2
    exit 1
fi

export IDF_TARGET="${ZECTRIX_IDF_TARGET:-esp32s3}"
export IDF_SKIP_CHECK_SUBMODULES="${IDF_SKIP_CHECK_SUBMODULES:-1}"

cd "$repo_dir"
if [ "$clean_build" -eq 1 ]; then
    if [ -d "$build_dir" ] && [ ! -f "$build_dir/CMakeCache.txt" ]; then
        if [ -L "$build_dir" ]; then
            printf 'Refusing to clean symlinked build directory: %s\n' "$build_dir" >&2
            exit 1
        fi
        cmake -E remove_directory "$build_dir"
    fi
    idf.py --ccache fullclean
fi
idf.py --ccache build

actual_target="$(jq -r '.target // .project_target // "unknown"' build/project_description.json)"
if [ "$actual_target" != "$IDF_TARGET" ]; then
    printf 'Built wrong target: expected=%s actual=%s\n' "$IDF_TARGET" "$actual_target" >&2
    exit 1
fi

printf 'PASS: firmware build target=%s ccache=enabled\n' "$actual_target"
