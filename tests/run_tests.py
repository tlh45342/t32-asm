#!/usr/bin/env python3
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
exe = root / ("t32-asm.exe" if os.name == "nt" else "t32-asm")
out = root / "tests" / "out"
out.mkdir(parents=True, exist_ok=True)
binary = out / "org-test.bin"

result = subprocess.run(
    [str(exe), "-f", "bin", str(root / "tests" / "org-test.s"), "-o", str(binary)],
    text=True,
    capture_output=True,
)
if result.returncode != 0:
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    raise SystemExit(result.returncode)

data = binary.read_bytes()
expected_target = 0x100C
actual_target = int.from_bytes(data[4:8], "little") if len(data) >= 8 else None

checks = [
    ("compact flat binary", len(data) == 24, f"size={len(data)}"),
    (".org label address", actual_target == expected_target,
     f"target=0x{actual_target:08x}" if actual_target is not None else "missing target"),
]

ok = True
for name, passed, detail in checks:
    print(("PASS" if passed else "FAIL"), name, f"({detail})")
    ok &= passed

raise SystemExit(0 if ok else 1)
