# TODO

## Decide: `.extern foo` and `foo:` in the same file

- The spec doesn't forbid it — `.global` exports, `.extern` imports, nothing more.
  GNU `as` ignores `.extern` entirely. So this is my call to make.
- It matters because the relocation rule ([DESIGN.md](DESIGN.md)) branches on exactly this bit:
  defined here → relocate against the **section symbol** (addend = offset);
  undefined/extern → relocate against **the symbol itself** (addend 0). A symbol
  that is both leaves `relocateAgainst()` without a rule.
- Options: (1) hard error; (2) local definition wins, `.extern` ignored (GNU
  behaviour); (3) extern wins — never, it would import a symbol we defined.
- Leaning (1): if the "both" state can't exist, `relocateAgainst` has two cases
  forever and no precedence rule to remember.
- Whichever way, the check must be **symmetric** — right now neither ordering is
  caught: `defineLabel` ignores `is_extern`, `declareExtern` ignores `is_defined`.
- Not urgent: nothing reads `is_extern` until relocations land.
- Once decided, move the resolution to `DESIGN.md`.
- Introduce Byte for std::uint8_t and Word for std::uint32_t and refactor the code so it uses the new types, for readability

## [low] `.equ` symbols and literals don't share a pool slot

- The original worry here — an absolute `.equ` symbol shipping as an `R_32`
  against a symbol with no section, leaving the linker a case to invent a rule
  for — is **fixed**. `flushLiteralPools` now emits `sym.value` directly when
  `section_index == ABS` ([assembler.cpp:396-400](src/asembler/assembler.cpp#L396-L400)),
  so no relocation is generated.
- Fixed at *emit* time rather than at *entry* time, though. The plan had been
  to make `getOrCreatePoolEntry` build a `LITERAL` entry holding the value, in
  which case an `.equ` symbol and a literal of the same value would dedup into
  one slot for free.
- As it stands they don't: `LiteralPoolEntry` equality compares `is_symbol`
  first, so `.equ x, 5` and a literal `5` in the same section take two words.
- Emitted bytes are correct either way. This is a handful of wasted pool
  bytes, nothing more — only worth doing if the pool entry code is being
  touched anyway.

## [low] Pool slot lookup by SymIndex

- `resolveJumpFixups` holds a `SymIndex` but has to package the name into a
  throwaway `InstructionOperand`, so `getOrCreatePoolEntry` can string-search it
  back into the index we already had. Correct, just a round-trip.
- Split the find-or-append core out and give it two front doors:

```cpp
std::uint16_t getOrCreatePoolSlot(const LiteralPoolEntry &wanted);       // core: the std::find + push_back
std::uint16_t getOrCreatePoolEntry(const InstructionOperand &operand);   // existing
std::uint16_t getOrCreatePoolEntry(SymIndex symbol);                     // new
```

- The operand version delegates its symbol branch to the new one, so how a
  symbol pool entry is built stops existing in two places — that duplication is
  what caused the earlier dedup bug.
- Purely QOL, emitted bytes are already correct. Do it after the conditional
  branches land.

## [assembler] `[%reg + <simbol>]` with a forward `.equ` is rejected

- `displacementFromSymbol` asks for the value at *emit* time, so a `.equ` defined
  **later** in the file errors even though it resolves fine by `.end`. A `.equ`
  defined earlier already works, and a label is a spec-mandated error either way
  (the spec's footnote on `[%reg + <simbol>]`).
- Defensible as-is: the footnote says to error when the value "nije poznata u
  trenutku asembliranja", and erroring at the instruction is one reading of that.
- Fix if a hidden test needs it: give the operand a fixup like `JumpFixup` —
  record `{instruction_offset, symbol}`, emit displacement 0, and patch in a pass
  that runs *after* `resolveEquSymbols`. Reuse the 12 bit splice already in
  `flushLiteralPools`; factor it into `patchDisplacement(section, offset, disp)`
  rather than copying it. ~35 lines.
- Stale marker sits at the `RegisterSymbolOffset` case in `instructionLoad`.

## [optional] End assembly at EOF, for files with no `.end`

- `.end` is the only thing that calls `endAssembly`, so a file without it exits 0
  and writes nothing at all. Every public test has `.end`, so this is a strict
  addition — no specified behaviour changes — and the spec says to
  follow GNU as on undefined details, which assembles fine with no end marker.
- Two edits: a `bool assembly_ended` guard at the top of `endAssembly` so it is
  idempotent, and a call from `main.cpp` after `parse()` returns 0.
- The guard is what makes it safe. The passes are not idempotent — running
  `endAssembly` twice appends the literal pool a second time and re-patches the
  instruction to point at the duplicate. `YYACCEPT` on `.end` already prevents
  that today, but the EOF path would reintroduce it without the guard.
- Skip the call and error instead if you would rather a truncated file fail
  loudly: `.end` is the only signal that a file is complete. Same one line.
