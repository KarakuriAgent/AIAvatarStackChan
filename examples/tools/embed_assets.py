#!/usr/bin/env python3
"""Generate C++ BuiltinAsset files from files in a directory."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def symbol_name(path: Path) -> str:
    name = re.sub(r"[^0-9A-Za-z_]", "_", path.as_posix())
    name = re.sub(r"_+", "_", name).strip("_").lower()
    if not name or name[0].isdigit():
        name = f"asset_{name}"
    return name


def format_bytes(data: bytes) -> list[str]:
    lines: list[str] = []
    for offset in range(0, len(data), 12):
        chunk = data[offset : offset + 12]
        lines.append("    " + ", ".join(f"0x{byte:02x}" for byte in chunk) + ",")
    return lines


def collect_files(input_dir: Path, suffixes: set[str]) -> list[Path]:
    files = [p for p in input_dir.rglob("*") if p.is_file() and p.suffix.lower() in suffixes]
    return sorted(files, key=lambda p: p.relative_to(input_dir).as_posix())


def write_header(output_base: Path, namespace: str, array_name: str) -> None:
    header = output_base.with_suffix(".h")
    guard = re.sub(r"[^0-9A-Za-z_]", "_", header.name).upper()
    header.write_text(
        "\n".join(
            [
                f"#ifndef {guard}",
                f"#define {guard}",
                "",
                '#include "AIAvatarStackChan.h"',
                "",
                f"namespace {namespace} {{",
                "",
                f"extern const BuiltinAsset {array_name}[];",
                f"extern const size_t {array_name}Count;",
                "",
                f"}}  // namespace {namespace}",
                "",
                f"#endif  // {guard}",
                "",
            ]
        ),
        encoding="utf-8",
    )


def write_cpp(
    input_dir: Path,
    files: list[Path],
    output_base: Path,
    namespace: str,
    array_name: str,
    path_prefix: str,
) -> None:
    cpp = output_base.with_suffix(".cpp")
    header_name = output_base.with_suffix(".h").name
    lines: list[str] = [
        f'#include "{header_name}"',
        "",
        "#include <pgmspace.h>",
        "",
        f"namespace {namespace} {{",
        "",
    ]

    entries: list[tuple[str, str, int]] = []
    for file_path in files:
        rel = file_path.relative_to(input_dir)
        symbol = symbol_name(rel)
        data = file_path.read_bytes()
        lines.append(f"static const uint8_t {symbol}[] PROGMEM = {{")
        lines.extend(format_bytes(data))
        lines.append("};")
        lines.append("")
        asset_path = f"{path_prefix.rstrip('/')}/{rel.as_posix()}"
        entries.append((asset_path, symbol, len(data)))

    lines.append(f"const BuiltinAsset {array_name}[] = {{")
    for asset_path, symbol, _ in entries:
        lines.append(f'    {{"{asset_path}", {symbol}, sizeof({symbol})}},')
    lines.append("};")
    lines.append("")
    lines.append(f"const size_t {array_name}Count = sizeof({array_name}) / sizeof({array_name}[0]);")
    lines.append("")
    lines.append(f"}}  // namespace {namespace}")
    lines.append("")

    cpp.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="Directory containing assets")
    parser.add_argument(
        "--output",
        default=Path("BuiltinAvatarImages"),
        type=Path,
        help="Output path without extension",
    )
    parser.add_argument("--namespace", default="aiavatar", help="C++ namespace")
    parser.add_argument("--array-name", default="kBuiltinAssets", help="BuiltinAsset array name")
    parser.add_argument("--path-prefix", default="/avatar", help="Logical asset path prefix")
    parser.add_argument(
        "--suffix",
        action="append",
        default=[".png", ".json"],
        help="File suffix to include; repeatable",
    )
    args = parser.parse_args()

    input_dir = args.input.resolve()
    if not input_dir.is_dir():
        raise SystemExit(f"input is not a directory: {input_dir}")

    output_base = args.output
    output_base.parent.mkdir(parents=True, exist_ok=True)
    suffixes = {suffix.lower() if suffix.startswith(".") else f".{suffix.lower()}" for suffix in args.suffix}
    files = collect_files(input_dir, suffixes)
    if not files:
        raise SystemExit(f"no matching files in {input_dir}")

    write_header(output_base, args.namespace, args.array_name)
    write_cpp(input_dir, files, output_base, args.namespace, args.array_name, args.path_prefix)
    print(f"generated {output_base.with_suffix('.h')} and {output_base.with_suffix('.cpp')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
