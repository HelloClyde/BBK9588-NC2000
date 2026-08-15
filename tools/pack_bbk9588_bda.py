from __future__ import annotations

import argparse
import sys
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description="Pack a linked BBK9588 raw image as BDA")
    parser.add_argument("raw", type=Path)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--title", required=True)
    parser.add_argument("--category", type=int, default=4)
    parser.add_argument("--icon", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    sys.path.insert(0, str(args.sdk.resolve()))
    from bda_packer.build import (  # type: ignore
        ENTRY_OFFSET,
        ICON_SIZES,
        ICON_START,
        build_icons,
    )
    from bda_packer.header import BdaHeaderFields, write_header  # type: ignore
    from bda_packer.validate import validate_bda  # type: ignore

    payload = args.raw.read_bytes()
    data = bytearray(b"\0" * ENTRY_OFFSET)
    icons = build_icons(args.icon, (0, 0, 0))
    if len(icons) != ENTRY_OFFSET - ICON_START:
        raise SystemExit("SDK icon layout does not match the expected BDA entry offset")
    data[ICON_START:ENTRY_OFFSET] = icons
    data.extend(payload)
    while len(data) & 3:
        data.append(0)

    fields = BdaHeaderFields(
        category=args.category,
        file_size_minus_4=len(data) - 4,
        entry_offset=ENTRY_OFFSET,
        icon_start=ICON_START,
        icon0_size=ICON_SIZES[0],
        icon1_size=ICON_SIZES[1],
        icon2_size=ICON_SIZES[2],
        icon3_size=ICON_SIZES[3],
    )
    write_header(data, fields, args.title)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    report = validate_bda(args.output)
    if not report["ok"]:
        args.output.unlink(missing_ok=True)
        raise SystemExit("BDA validation failed: " + "; ".join(report["errors"]))
    print(f"BDA: {args.output}")
    print(f"size: {len(data)} bytes")


if __name__ == "__main__":
    main()
