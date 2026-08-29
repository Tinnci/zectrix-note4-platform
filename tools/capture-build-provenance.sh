#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
build_dir="${1:-$repo_dir/build}"
output_file="${2:-$build_dir/build-provenance.txt}"

sha256_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{ print $1 }'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{ print $1 }'
    else
        openssl dgst -sha256 "$1" | awk '{ print $NF }'
    fi
}

test -d "$build_dir"
test -f "$build_dir/project_description.json"

{
    printf 'repo=%s\n' "$repo_dir"
    printf 'git_commit=%s\n' "$(git -C "$repo_dir" rev-parse HEAD)"
    printf 'git_status=%s\n' "$(git -C "$repo_dir" status --porcelain | tr '\n' ';')"
    printf 'idf_path=%s\n' "${IDF_PATH:-unset}"
    printf 'idf_version=%s\n' "$(idf.py --version 2>/dev/null || true)"
    printf 'idf_commit=%s\n' "$(git -C "${IDF_PATH:?IDF_PATH is not set}" rev-parse HEAD)"
    printf 'idf_tag=%s\n' "$(git -C "$IDF_PATH" describe --tags --exact-match 2>/dev/null || true)"
    printf 'target=%s\n' "$(jq -r '.project_target // .target // "unknown"' "$build_dir/project_description.json")"
    printf 'compiler=%s\n' "$(command -v xtensa-esp32s3-elf-gcc || true)"
    printf 'cmake=%s\n' "$(cmake --version | head -n 1)"
    printf 'ninja=%s\n' "$(ninja --version)"
    printf 'dependencies_lock_sha256=%s\n' "$(sha256_file "$repo_dir/dependencies.lock")"
    printf '\nartifacts:\n'
    find "$build_dir" -maxdepth 3 -type f \( -name '*.bin' -o -name '*.elf' -o -name '*.map' \) -print |
        LC_ALL=C sort |
        while IFS= read -r artifact; do
            printf '%s  %s\n' "$(sha256_file "$artifact")" "$artifact"
        done
} > "$output_file"

printf 'Wrote %s\n' "$output_file"
