# Assembler, linker and emulator for a 32-bit ISA

A complete toolchain for a small custom 32-bit processor, written in C++17:

- **`asembler`**: a single-pass assembler that turns assembly source into
  relocatable object files.
- **`linker`**: merges object files, resolves symbols and relocations, and
  outputs either a memory image (`-hex`) or another relocatable object
  (`-relocatable`).
- **`emulator`**: an interpreter that loads the memory image and runs it,
  with a memory-mapped terminal and timer driven by interrupts.

## Quick start

Requires `g++` with C++17, GNU `make`, `flex` and `bison` (3.5 or newer).

```sh
make            # builds bin/asembler, bin/linker, bin/emulator
make help       # other targets
```

Assemble, link and run a program:

```sh
./bin/asembler -o main.o main.s
./bin/asembler -o lib.o  lib.s
./bin/linker -hex -place=code@0x40000000 -o program.hex main.o lib.o
./bin/emulator program.hex
```

When the program executes `halt`, the emulator prints the register state:

```
Emulated processor executed halt instruction
Emulated processor state:
r0=0x00000000 r1=0x00000015 r2=0x00000003 r3=0x00000000
...
```

## The target machine

- 32-bit words, byte addressed, little-endian, 4 GB address space.
- 16 general-purpose registers. `r0` is hardwired to zero, `r14` is `sp`
  (the stack grows downwards) and `r15` is `pc`. Execution starts at
  `0x40000000`.
- 3 control registers: `status` (interrupt masks), `handler` (address of the
  single interrupt routine) and `cause` (why it was entered: bad instruction,
  timer, terminal or software `int`).
- Every instruction is 4 bytes: `OC | MOD | RegA | RegB | RegC | Disp[11:0]`.
  The 12-bit displacement is signed.
- Peripherals are memory-mapped at the top of the address space: `term_out`
  at `0xFFFFFF00`, `term_in` at `0xFFFFFF04` and `tim_cfg` at `0xFFFFFF10`.

The assembly language has the usual directives (`.global`, `.extern`,
`.section`, `.word`, `.skip`, `.ascii`, `.equ`, `.end`) and 26 instructions:
control flow, push/pop, arithmetic, logic, shifts, `xchg`, loads and stores
with eight addressing modes, and control-register access.

## Usage

```
asembler -o <output.o> <input.s>
linker   (-hex | -relocatable) [-place=<section>@<address>]... -o <output> <input.o>...
emulator <program.hex>
```

- **`-hex`** places every section and writes the memory image. Sections named
  by `-place` go at their address. The rest are packed one after another,
  starting after the highest placed section. Same-named sections from
  different files are concatenated in command-line order. The link fails on an
  undefined symbol, a duplicate definition or overlapping sections.
- **`-relocatable`** merges the inputs into one object file without placing
  anything. The result can be passed to the linker again.

## How it works

Some of the more interesting decisions. [DESIGN.md](DESIGN.md) covers them in
detail.

- **Literal pools.** A 12-bit displacement can't hold a full 32-bit constant
  or address. Values that don't fit are put in a pool at the end of their
  section and loaded pc-relative. A displacement that is still out of range is
  a hard error, never silently wrong code.
- **One relocation type.** Every relocation is `R_32` (`S + A`) with the addend
  stored in the record. A symbol defined in the same file is relocated against
  its *section*, with its offset as the addend. Only true imports are relocated
  against the symbol itself. Because of this, local labels never have to
  survive into a merged object, which keeps `-relocatable` simple.
- **A plain-text object format**, readable and diffable. Each format has
  exactly one reader and one writer, shared by the tools that use it. The
  object file is shared by the assembler and the linker, and the hex image by
  the linker and the emulator. Each reader checks its input as it reads, so a
  malformed file fails right away with a file:line message. A sample object
  file:

  ```
  SYMTAB 3
  1 00000000 SCTN LOC 1 lib
  2 00000000 NOTYP GLOB 1 addTwo
  3 0000000c NOTYP GLOB 1 mulThree

  SECTION lib 24
  91 20 00 02 50 11 20 00 93 fe 00 04 91 20 00 03
  52 11 20 00 93 fe 00 04
  ```

- **`.equ` with expressions.** An expression can combine symbols and literals
  with `+` and `-`, and is resolved at `.end`. As in GNU `as`, the result must
  be either absolute or relative to a single section.
- **The emulator** keeps memory sparse, allocating 4 KB pages on first write.
  It puts the host terminal in raw, non-blocking mode for the duration of the
  run, so keys reach the program the moment they are pressed. It checks the keyboard and timer every
  256 instructions rather than after every one, which made it about 3× faster.

## Tests

`tests/javni_testovi/` holds the course's public tests, levels A to C. Each
directory has a `start.sh` that assembles, links and runs it:

```sh
cd tests/javni_testovi/nivo-a && ./start.sh
```

Levels B and C are interactive. B waits for `y` to be pressed 20 times, and C
for any 20 keys while the timer prints messages.

`tests/start.sh` runs my own tests: instructions the public tests never
reach, the software and bad-instruction interrupts, and a `-relocatable`
round trip. The expected results are written at the top of each test file.

## Project layout

```
misc/       flex lexer and bison grammar for the assembler
inc/        headers
src/        one directory per tool, plus common/ for the shared file formats
tests/      public tests and my own
```

## Known limitations

A few corner cases are handled conservatively rather than fully. For example,
a `[%reg + symbol]` operand whose `.equ` symbol is defined later in the file is
rejected, and a file without `.end` produces no output. They are listed with
possible fixes in [todo.md](todo.md).

## Background

This was the semester project for *Systems Software* (13E113SS) at the
School of Electrical Engineering, University of Belgrade. The tool names, the
ISA and the command-line interface are fixed by the assignment. All the design
choices inside them were left to the student.
