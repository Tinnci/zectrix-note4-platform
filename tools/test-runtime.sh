#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build="$repo/build-runtime-host"
flags=""
if [ "${ZECTRIX_RUNTIME_SANITIZE:-0}" = 1 ]; then
    build="$repo/build-runtime-host-sanitized"
    flags="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
fi
args=(-S "$repo/tools/runtime-host" -B "$build" -DCMAKE_BUILD_TYPE=Debug
    "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_CXX_FLAGS=$flags" "-DCMAKE_EXE_LINKER_FLAGS=$flags")
if [ -n "${ZECTRIX_LUA_SOURCE_DIR:-}" ]; then
    args+=("-DZECTRIX_LUA_SOURCE_DIR=$ZECTRIX_LUA_SOURCE_DIR")
fi
cmake "${args[@]}" > "$build-configure.log" 2>&1 || { cat "$build-configure.log"; exit 1; }
cmake --build "$build" --parallel 4 > "$build-compile.log" 2>&1 || { cat "$build-compile.log"; exit 1; }
ctest --test-dir "$build" --output-on-failure
printf 'PASS: sandboxed micro-app runtime.\n'
