# /// script
# requires-python = ">=3.11"
# dependencies = ["Markdown==3.9"]
# ///
"""Export the bilingual handbook as standalone HTML with embedded screen images."""

import argparse
import base64
import html
from pathlib import Path
import re
from urllib.parse import quote, urlsplit

import markdown

ROOT = Path(__file__).resolve().parent.parent
STYLE = """html { color-scheme: light; background: #f3f1eb; color: #20251f; }
body { margin: 0 auto; padding: 2rem 1.25rem 5rem; max-width: 76ch;
       font: 17px/1.7 system-ui, sans-serif; overflow-wrap: anywhere; }
h1, h2, h3 { line-height: 1.25; margin-top: 2em; color: #244735; }
h1 { font-size: 2.2rem; } a { color: #17643e; } h2, h3 { scroll-margin-top: 1rem; }
img { display: block; width: 400px; max-width: 100%; height: auto;
      border: 1px solid #7b8176; margin: 1.5rem auto; }
table { display: block; overflow-x: auto; border-collapse: collapse; font-size: .94rem; }
td, th { border: 1px solid #c8ccbe; padding: .6rem .8rem; text-align: left; }
th { background: #e4e9dc; } pre { overflow-x: auto; padding: 1rem; background: #e7eadf; }
code { font-size: .87em; } blockquote { border-left: 3px solid #7c8f6c; padding-left: 1rem; margin-left: 0; }
.edition { color: #58614f; font-size: .9rem; } .toc { padding: .5rem 1.5rem; background: #e7eadf; }
@media print { html { background: white; } body { max-width: none; font-size: 11pt; }
              pre { white-space: pre-wrap; overflow: visible; }
              table { display: table; width: 100%; }
              img, tr, pre { break-inside: avoid; } h2, h3 { break-after: avoid; } }
"""


def export(version, output):
    output.mkdir(parents=True, exist_ok=True)
    documents = {"HANDBOOK.md": "en", "HANDBOOK_zh.md": "zh-CN"}
    names = {name: f"zectrix-note4-{version}-handbook-{language}.html"
             for name, language in documents.items()}
    for name, language in documents.items():
        source = ROOT / "docs" / name
        body = markdown.markdown(source.read_text(encoding="utf-8"),
                                 extensions=["tables", "fenced_code", "toc", "sane_lists"])

        def link(match):
            prefix, attribute, value, suffix = match.groups()
            url = urlsplit(html.unescape(value))
            if url.scheme or url.netloc or not url.path:
                return match.group(0)
            path = (source.parent / url.path).resolve()
            relative = path.relative_to(ROOT).as_posix()
            if attribute == "src":
                if path.suffix != ".png":
                    raise ValueError(f"Expected a PNG illustration: {path}")
                value = "data:image/png;base64," + base64.b64encode(path.read_bytes()).decode("ascii")
            elif path.name in names and path.parent == source.parent:
                value = names[path.name]
            else:
                value = f"https://github.com/Tinnci/zectrix-note4-platform/blob/{quote(version, safe='')}/{quote(relative)}"
            if url.fragment:
                value += "#" + url.fragment
            return prefix + html.escape(value, quote=True) + suffix

        body = re.sub(r'(<(?:a|img)\b[^>]*\b(href|src)=")([^"]+)(")', link, body)
        title = "Note4 用户与开发手册" if language == "zh-CN" else "Note4 user and developer handbook"
        document = (f'<!doctype html>\n<html lang="{language}"><head><meta charset="utf-8">'
                    '<meta name="viewport" content="width=device-width,initial-scale=1">'
                    f"<title>{title}</title><style>{STYLE}</style></head><body>"
                    f'<p class="edition">Note4 Open Platform · {html.escape(version)}</p>'
                    f"{body}</body></html>\n")
        (output / names[name]).write_text(document, encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._+-]{0,63}", args.version):
        parser.error("Use a filename-safe release version")
    export(args.version, args.output)
    print(f"Exported English and Chinese handbooks to {args.output}")
