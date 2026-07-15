# t32-asm Version History

## 0.0.6

- Added functional `.org ADDRESS` support to the actual built source file.
- `.org` changes logical label addresses without adding leading padding to flat binaries.
- Restricted `.org` to one occurrence before emitted code or data.
- Wired `--version` to `src/include/version.h`.
- Added `version.h` as an object dependency in the Makefile.
- Added an automated `.org 0x1000` regression test.

## 0.0.5

- Introduced `src/include/version.h`.

## 0.0.4

- Adopted the canonical T32 opcode map.
