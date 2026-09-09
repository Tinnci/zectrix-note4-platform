"""Resolve project module dependencies before ESP-IDF expands REQUIRES."""

import argparse
import tempfile
from pathlib import Path

import kconfiglib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--kconfig", required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--defaults", action="append", default=[])
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    config = kconfiglib.Kconfig(args.kconfig)
    # SDK symbols are handled by IDF's full pass, not this module-only pass.
    config.warn_assign_undef = False
    config.warn_assign_override = False
    config.warn_assign_redun = False
    # IDF's kconfgen treats empty defaults assignments as disabled booleans.
    with tempfile.TemporaryDirectory() as directory:
        defaults = Path(directory) / "sdkconfig.defaults"
        for path in args.defaults:
            lines = (line.strip() for line in Path(path).read_text().splitlines())
            defaults.write_text("".join(line + ("n" if line.endswith("=") else "") + "\n"
                                        for line in lines))
            config.load_config(str(defaults), replace=False)
    if Path(args.config).exists():
        config.load_config(args.config, replace=False)

    content = "# Generated from the component Kconfig definitions.\n"
    for symbol in config.unique_defined_syms:
        if symbol.name.startswith("ZECTRIX_ENABLE_"):
            content += f"set(CONFIG_{symbol.name} {1 if symbol.str_value == 'y' else 0})\n"
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text() != content:
        output.write_text(content)


if __name__ == "__main__":
    main()
