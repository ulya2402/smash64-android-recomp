#!/usr/bin/env python3
"""Verify N64 ROM header and SHA-256 without copying copyrighted assets into the repo."""

import hashlib
import sys
from pathlib import Path

EXPECTED_NALE_SHA256 = "15592E79D3C5295CEF4371D4992F0BD25BEC2102FC29644C93E682F7EA99EF3D"


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: python verify_rom.py <path-to-rom.z64>")
        return 1

    rom_path = Path(sys.argv[1])
    if not rom_path.is_file():
        print(f"Error: File not found: {rom_path}")
        return 1

    data = rom_path.read_bytes()
    header = data[:64]
    magic = header[:4].hex().upper()
    title = header[32:52].decode("ascii", errors="replace").strip()
    game_id = header[59:63].decode("ascii", errors="replace")
    sha256 = hashlib.sha256(data).hexdigest().upper()

    print(f"File   : {rom_path}")
    print(f"Size   : {len(data)} bytes")
    print(f"Magic  : {magic}")
    print(f"Title  : {title}")
    print(f"GameID : {game_id}")
    print(f"SHA256 : {sha256}")
    print(f"Match  : {sha256 == EXPECTED_NALE_SHA256}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
