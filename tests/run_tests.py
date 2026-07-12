#!/usr/bin/env python3

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXE = ROOT / ("t32-asm.exe" if sys.platform == "win32" else "t32-asm")
SOURCE = ROOT / "tests" / "basic.asm"
OUT = ROOT / "tests" / "out"
EXPECTED = bytes(
    [
        0x00, 0x00, 0x00, 0x02,
        0x2F, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x10, 0x01,
        0x00, 0x00, 0x00, 0x00,
    ]
)


def run(command: list[str]) -> None:
    subprocess.run(command, cwd=ROOT, check=True)


def verify(path: Path) -> None:
    actual = path.read_bytes()
    if actual != EXPECTED:
        raise RuntimeError(
            f"{path.name}: binary mismatch\n"
            f"expected={EXPECTED.hex()}\n"
            f"actual  ={actual.hex()}"
        )


def main() -> int:
    OUT.mkdir(parents=True, exist_ok=True)

    positional = OUT / "positional.t32"
    option_form = OUT / "option.bin"
    long_form = OUT / "long.bin"

    run([str(EXE), str(SOURCE), str(positional)])
    run([
        str(EXE),
        "-f", "bin",
        str(SOURCE),
        "-o", str(option_form),
    ])
    run([
        str(EXE),
        "--format", "bin",
        "--output", str(long_form),
        str(SOURCE),
    ])

    for path in (positional, option_form, long_form):
        verify(path)
        print(f"PASS {path.name}")

    bad = subprocess.run(
        [str(EXE), "-f", "obj", str(SOURCE), "-o", str(OUT / "bad.o")],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )

    if bad.returncode == 0:
        print("FAIL unsupported-format")
        return 1

    if "unsupported output format: obj" not in bad.stdout:
        print("FAIL unsupported-format message")
        print(bad.stdout)
        return 1

    print("PASS unsupported-format")
    print("\nPASS t32-asm command-line tests")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
