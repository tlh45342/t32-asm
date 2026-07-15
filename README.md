# t32-asm

`t32-asm` is the assembler for the T32 instruction set and is part of the
Foundry project.

Clone the repository:

```bash
git clone https://github.com/tlh45342/t32-asm.git
```

## Build

```bash
make
make test
make install
```

On Linux and macOS, the default install location is:

```text
~/.local/bin/t32-asm
```

Ensure that directory is on your path:

```bash
export PATH="$HOME/.local/bin:$PATH"
echo "$PATH"
```

## Usage

The original positional form remains supported:

```bash
t32-asm input.s output.t32
```

The preferred test/toolchain form is:

```bash
t32-asm -f bin input.s -o output.bin
```

Long options are also supported:

```bash
t32-asm --format bin --output output.bin input.asm
```

Current options:

```text
-f, --format <format>   Output format
-o, --output <file>     Output file
-v, --verbose           Show selected paths and format
    --version           Show version
-h, --help              Show help
```

Current formats:

```text
bin    Flat, directly loadable T32 binary
```

The command-line parser is intentionally ready for a future relocatable object
format:

```text
-f obj
```

but `obj` is not implemented in this release.

## Compatibility

Both of these produce the same flat binary:

```bash
t32-asm program.asm program.t32
t32-asm -f bin program.asm -o program.t32
```
