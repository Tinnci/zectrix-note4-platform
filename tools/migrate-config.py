#!/usr/bin/env python3
"""Copy legacy Kconfig keys to NOTE4 names without overwriting the source."""

import argparse
from pathlib import Path
import re


def migrate_config(text: str) -> str:
    migrated = re.sub(r"(?m)^((?:# )?CONFIG_)ZECTRIX_DEMO_", r"\1NOTE4_QUALIFICATION_", text)
    migrated = re.sub(r"(?m)^((?:# )?CONFIG_)ZECTRIX_", r"\1NOTE4_", migrated)
    values = {}
    for line in migrated.splitlines():
        match = re.fullmatch(r"(# )?(CONFIG_NOTE4_[A-Z0-9_]+)(=.*| is not set)", line)
        if not match:
            continue
        key, value = match[2], "=n" if match[1] else match[3]
        if key in values and values[key] != value:
            raise ValueError(f"Conflicting legacy/new values for {key}")
        values[key] = value
    return migrated


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        text = migrate_config(args.input.read_text())
        args.output.parent.mkdir(parents=True, exist_ok=True)
        # Exclusive creation also refuses symlinks and racing invocations.
        with args.output.open("x") as output:
            output.write(text)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f"Migrated configuration to {args.output}; source unchanged.")


if __name__ == "__main__":
    main()
