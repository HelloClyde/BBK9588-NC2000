from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys


TARGET_DIRECTORY = "/NC2000"
ROM_SUFFIXES = ("nand", "nand0", "nor")


def digest_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def digest_nand_file(fs, path: str) -> str:
    digest = hashlib.sha256()
    with fs.openbin(path, "r") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    parser = argparse.ArgumentParser(
        description="把一组 NC2000 ROM 的副本写入 BBK9588 模拟器 NAND。"
    )
    parser.add_argument("--emulator-root", type=Path, required=True)
    parser.add_argument("--nand", type=Path, required=True)
    parser.add_argument("--rom-dir", type=Path, required=True)
    parser.add_argument("--base-name", default="35")
    args = parser.parse_args()

    emulator_root = args.emulator_root.resolve()
    nand = args.nand.resolve()
    rom_dir = args.rom_dir.resolve()
    if not emulator_root.is_dir():
        raise SystemExit(f"模拟器目录不存在：{emulator_root}")
    if not nand.is_file():
        raise SystemExit(f"测试 NAND 不存在：{nand}")
    if not rom_dir.is_dir():
        raise SystemExit(f"ROM 目录不存在：{rom_dir}")
    if not args.base_name or any(ch in args.base_name for ch in "/\\"):
        raise SystemExit("base-name 只能是文件基本名")

    sources = {
        suffix: rom_dir / f"{args.base_name}.{suffix}" for suffix in ROM_SUFFIXES
    }
    for suffix, source in sources.items():
        if not source.is_file():
            raise SystemExit(f"缺少 ROM：{source}")
    if sources["nand"].stat().st_size < 65536 * 528:
        raise SystemExit(".nand 小于 NC2000 所需的 65,536 × 528 byte")
    if sources["nand0"].stat().st_size <= 0:
        raise SystemExit(".nand0 为空")
    if sources["nor"].stat().st_size != 512 * 1024:
        raise SystemExit(".nor 必须是 524,288 byte")

    metadata = {
        suffix: {
            "source": str(source),
            "size": source.stat().st_size,
            "sha256": digest_file(source),
            "target": f"{TARGET_DIRECTORY}/{args.base_name}.{suffix}",
        }
        for suffix, source in sources.items()
    }

    sys.path.insert(0, str(emulator_root))
    from emu.qemu.nand_fs import mutate_nand_files, replace_fat_file

    def operation(fs) -> None:
        if not fs.exists(TARGET_DIRECTORY):
            fs.makedir(TARGET_DIRECTORY, recreate=False)
        elif not fs.isdir(TARGET_DIRECTORY):
            raise NotADirectoryError(f"NAND 路径不是目录：{TARGET_DIRECTORY}")
        for suffix in ROM_SUFFIXES:
            with sources[suffix].open("rb") as stream:
                replace_fat_file(fs, metadata[suffix]["target"], stream)

    def validator(fs) -> None:
        for suffix in ROM_SUFFIXES:
            item = metadata[suffix]
            target = item["target"]
            if not fs.isfile(target):
                raise ValueError(f"写入后的 ROM 不存在：{target}")
            if fs.getsize(target) != item["size"]:
                raise ValueError(f"写入后的 ROM 大小错误：{target}")
            if digest_nand_file(fs, target) != item["sha256"]:
                raise ValueError(f"写入后的 ROM SHA256 错误：{target}")

    mutate_nand_files(nand, operation, validator=validator)
    print(
        json.dumps(
            {
                "ok": True,
                "nand": str(nand),
                "base_name": args.base_name,
                "files": metadata,
            },
            ensure_ascii=False,
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
