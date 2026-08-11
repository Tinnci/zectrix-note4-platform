#!/usr/bin/env bash

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    printf 'Usage: source %s\n' "$0" >&2
    exit 2
fi

zectrix_idf_root="${ZECTRIX_IDF_PATH:-/home/drie/esp/esp-idf-v5.5.2}"
zectrix_cmake_bin="${ZECTRIX_CMAKE_BIN_DIR:-/home/drie/.local/venvs/zectrix-cmake-3.30.5/bin}"

if [ ! -f "$zectrix_idf_root/export.sh" ]; then
    printf 'ESP-IDF export.sh not found: %s\n' "$zectrix_idf_root/export.sh" >&2
    return 1
fi

# Keep the IDF Python environment ahead of the CMake venv, which also ships a Python binary.
source "$zectrix_idf_root/export.sh"
export PATH="$IDF_PYTHON_ENV_PATH/bin:$zectrix_cmake_bin:/home/drie/.local/bin:$PATH"
export ZECTRIX_IDF_PATH="$zectrix_idf_root"

printf 'Activated ESP-IDF %s with CMake from %s\n' "$IDF_PATH" "$zectrix_cmake_bin"
printf 'Run IDF commands with IDF_SKIP_CHECK_SUBMODULES=1 after check-dev-env.sh passes.\n'
