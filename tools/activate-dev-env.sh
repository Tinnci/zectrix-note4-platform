#!/usr/bin/env bash

if { [ -n "${BASH_VERSION:-}" ] && [ "${BASH_SOURCE[0]:-}" = "$0" ]; } ||
        { [ -n "${ZSH_VERSION:-}" ] && [[ "${ZSH_EVAL_CONTEXT:-}" != *:file ]]; }; then
    printf 'Usage: source %s\n' "$0" >&2
    exit 2
fi

zectrix_idf_root="${ZECTRIX_IDF_PATH:-$HOME/esp/esp-idf-v5.5.2}"
zectrix_cmake_bin="${ZECTRIX_CMAKE_BIN_DIR:-$HOME/.local/venvs/zectrix-cmake-3.30.5/bin}"
zectrix_host_os="$(uname -s)"
zectrix_idf_python_env="${ZECTRIX_IDF_PYTHON_ENV_PATH:-${IDF_PYTHON_ENV_PATH:-}}"

case "$zectrix_host_os" in
    Darwin) zectrix_android_default="$HOME/Library/Android/sdk" ;;
    Linux) zectrix_android_default="$HOME/Android/Sdk" ;;
    *)
        printf 'Unsupported host OS: %s (expected macOS or Linux)\n' "$zectrix_host_os" >&2
        return 1
        ;;
esac

# GUI applications and non-login CI shells on macOS often omit Homebrew from
# PATH even though the tools are installed. Add only standard existing roots.
for zectrix_host_bin in /opt/homebrew/bin /usr/local/bin "$HOME/.local/bin"; do
    if [ -d "$zectrix_host_bin" ]; then
        case ":$PATH:" in
            *":$zectrix_host_bin:"*) ;;
            *) export PATH="$zectrix_host_bin:$PATH" ;;
        esac
    fi
done

if [ ! -f "$zectrix_idf_root/export.sh" ]; then
    printf 'ESP-IDF export.sh not found: %s\n' "$zectrix_idf_root/export.sh" >&2
    return 1
fi
if [ ! -x "$zectrix_cmake_bin/cmake" ]; then
    printf 'Qualified CMake not found: %s\n' "$zectrix_cmake_bin/cmake" >&2
    return 1
fi

# ESP-IDF names its Python environment after the Python minor version. Reuse a
# single installed v5.5 environment so activation is independent of the host
# shell's initial Python PATH. Require an override if more than one exists.
if [ -z "$zectrix_idf_python_env" ] && [ -d "$HOME/.espressif/python_env" ]; then
    zectrix_idf_python_candidates="$(find "$HOME/.espressif/python_env" -maxdepth 1 \
        -type d -name 'idf5.5_py*_env' -print 2>/dev/null | LC_ALL=C sort)"
    zectrix_idf_python_count="$(printf '%s\n' "$zectrix_idf_python_candidates" |
        awk 'NF { count++ } END { print count + 0 }')"
    if [ "$zectrix_idf_python_count" -eq 1 ]; then
        zectrix_idf_python_env="$zectrix_idf_python_candidates"
    elif [ "$zectrix_idf_python_count" -gt 1 ]; then
        printf 'Multiple ESP-IDF v5.5 Python environments found; set ZECTRIX_IDF_PYTHON_ENV_PATH.\n' >&2
        return 1
    fi
fi
if [ -n "$zectrix_idf_python_env" ]; then
    if [ ! -x "$zectrix_idf_python_env/bin/python" ]; then
        printf 'ESP-IDF Python environment is invalid: %s\n' "$zectrix_idf_python_env" >&2
        return 1
    fi
    export IDF_PYTHON_ENV_PATH="$zectrix_idf_python_env"
    export PATH="$IDF_PYTHON_ENV_PATH/bin:$PATH"
fi

# Keep the IDF Python environment ahead of the CMake venv, which also ships a Python binary.
# shellcheck disable=SC1091
source "$zectrix_idf_root/export.sh" || return 1
: "${IDF_PYTHON_ENV_PATH:?ESP-IDF export did not set IDF_PYTHON_ENV_PATH}"
export PATH="$IDF_PYTHON_ENV_PATH/bin:$zectrix_cmake_bin:$HOME/.local/bin:$PATH"
export ZECTRIX_IDF_PATH="$zectrix_idf_root"
export ZECTRIX_CMAKE_BIN_DIR="$zectrix_cmake_bin"
export IDF_TARGET="${ZECTRIX_IDF_TARGET:-esp32s3}"
export ANDROID_HOME="${ZECTRIX_ANDROID_SDK_ROOT:-${ANDROID_HOME:-$zectrix_android_default}}"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
if [ -d "$ANDROID_HOME/platform-tools" ]; then
    export PATH="$ANDROID_HOME/platform-tools:$PATH"
fi

zectrix_java_home="${ZECTRIX_JAVA_HOME:-}"
zectrix_jenv="$(command -v jenv 2>/dev/null || true)"
if [ -z "$zectrix_jenv" ]; then
    for zectrix_jenv_candidate in "$HOME/.jenv/bin/jenv" /usr/local/bin/jenv /opt/homebrew/bin/jenv; do
        if [ -x "$zectrix_jenv_candidate" ]; then
            zectrix_jenv="$zectrix_jenv_candidate"
            break
        fi
    done
fi
if [ -z "$zectrix_java_home" ] && [ -n "${JAVA_HOME:-}" ] &&
        [ -x "$JAVA_HOME/bin/java" ] &&
        "$JAVA_HOME/bin/java" -version 2>&1 | head -n 1 | grep -Eq 'version "21([.]|\")'; then
    zectrix_java_home="$JAVA_HOME"
fi
if [ -z "$zectrix_java_home" ] && [ -x "$HOME/.jenv/versions/21/bin/java" ]; then
    zectrix_java_home="$HOME/.jenv/versions/21"
fi
if [ -z "$zectrix_java_home" ] && [ -n "$zectrix_jenv" ]; then
    zectrix_java_home="$("$zectrix_jenv" prefix 21 2>/dev/null || true)"
fi
if [ -z "$zectrix_java_home" ] && [ "$zectrix_host_os" = Darwin ] &&
        [ -x /usr/libexec/java_home ]; then
    zectrix_java_home="$(/usr/libexec/java_home -v 21 2>/dev/null || true)"
fi
if [ -z "$zectrix_java_home" ] && [ "$zectrix_host_os" = Linux ]; then
    for zectrix_java_candidate in \
        /usr/lib/jvm/java-21-openjdk \
        /usr/lib/jvm/java-21-openjdk-amd64 \
        /usr/lib/jvm/java-21-openjdk-*; do
        if [ -x "$zectrix_java_candidate/bin/java" ]; then
            zectrix_java_home="$zectrix_java_candidate"
            break
        fi
    done
fi
if [ -n "$zectrix_java_home" ] && [ -x "$zectrix_java_home/bin/java" ] &&
        "$zectrix_java_home/bin/java" -version 2>&1 | head -n 1 |
            grep -Eq 'version "21([.]|\")'; then
    export JAVA_HOME="$zectrix_java_home"
    export PATH="$JAVA_HOME/bin:$PATH"
else
    printf 'JDK 21 not found; set ZECTRIX_JAVA_HOME before activation.\n' >&2
    return 1
fi

printf 'Activated ESP-IDF %s for %s with CMake from %s\n' \
    "$IDF_PATH" "$IDF_TARGET" "$zectrix_cmake_bin"
printf 'Android SDK: %s; Java: %s\n' "$ANDROID_HOME" "${JAVA_HOME:-not found}"
printf 'Use tools/build-firmware.sh so target and ccache settings stay explicit.\n'

unset zectrix_android_default zectrix_cmake_bin zectrix_host_bin zectrix_host_os zectrix_idf_root
unset zectrix_idf_python_candidates zectrix_idf_python_count zectrix_idf_python_env
unset zectrix_java_candidate zectrix_java_home zectrix_jenv zectrix_jenv_candidate
