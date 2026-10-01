#!/usr/bin/env bash
# Build the same download set locally and in the GitHub release matrix.
set -euo pipefail
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
version="v$(cat "$repo_dir/version.txt")"
profiles=(full minimal reader)
output=""
build=1
usage() { printf 'Usage: %s [--version TAG] [--output DIR] [--profile full|minimal|reader] [--from-builds]\n' "$0"; }
while [ "$#" -gt 0 ]; do
    case "$1" in
        --version) version="${2:?Missing version}"; shift 2 ;;
        --output) output="${2:?Missing output directory}"; shift 2 ;;
        --profile)
            case "${2:-}" in
                full|minimal|reader) profiles=("$2"); shift 2 ;;
                *) usage >&2; exit 2 ;;
            esac ;;
        --from-builds) build=0; shift ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done
base="v$(cat "$repo_dir/version.txt")"
if ! [[ "$version" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[A-Za-z0-9]+([.-][A-Za-z0-9]+)*)?$ ]] ||
        ! [[ "$version" = "$base" || "$version" = "$base-"* ]]; then
    printf 'Release tag must match version.txt (%s), optionally with a prerelease suffix.\n' "$base" >&2
    exit 2
fi
output="${output:-$repo_dir/build-release-$version}"
if [ -e "$output" ]; then
    printf 'Output already exists: %s\n' "$output" >&2
    exit 1
fi
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT
args=(--version "$version" --output "$output")
for profile in "${profiles[@]}"; do
    if [ "$build" -eq 1 ]; then
        bash "$repo_dir/tools/build-firmware.sh" --profile "$profile"
        bash "$repo_dir/tools/capture-build-provenance.sh" "$repo_dir/build-$profile"
    fi
    args+=(--profile "$profile=$repo_dir/build-$profile")
    if [ "$profile" = full ]; then
        mkdir -p "$work_dir/extras"
        uv run --script "$repo_dir/tools/export-handbook.py" --version "$version" --output "$work_dir/extras"
        for app in Calculator Flashcards; do
            uv run --script "$repo_dir/tools/zapp.py" build "$repo_dir/apps/$app.app.json" -o "$work_dir/extras/$app.zapp"
        done
        uv run --no-project - "$repo_dir" "$version" "$work_dir/extras" <<'PY'
from pathlib import Path
import sys
import zipfile

root, version, output = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])
with zipfile.ZipFile(output / f"zectrix-note4-{version}-host-tools.zip", "w", zipfile.ZIP_DEFLATED) as archive:
    for name in ("tools/usb-manager.py", "tools/zapp.py", "tools/zapp", "LICENSE"):
        archive.write(root / name, name)
    archive.writestr("README.txt", """Note4 USB and application tools / USB 与应用工具
Install uv: https://docs.astral.sh/uv/
Open Tools > USB Manager on Note4, then run from this extracted directory:
在设备上打开 工具 > USB 管理，然后从本解压目录执行：
  uv run --script tools/usb-manager.py ports
  uv run --script tools/usb-manager.py --port PORT list
  uv run --script tools/usb-manager.py --port PORT put book.epub
  uv run --script tools/usb-manager.py --port PORT app-put Calculator.zapp
Use --help for export, settings and app packaging commands.
使用 --help 查看导出、设置和应用打包命令。PORT 为 Note4 串口。
Download Calculator.zapp separately and place it in this directory for the example command.
示例命令需另行下载 Calculator.zapp 并放在本目录中。
""")
PY
        args+=(--library --extras "$work_dir/extras")
    fi
done
uv run --no-project "$repo_dir/tools/firmware_package.py" "${args[@]}"
