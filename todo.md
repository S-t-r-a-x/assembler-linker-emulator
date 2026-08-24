# TODO

## Decide: `.extern foo` and `foo:` in the same file

- The spec doesn't forbid it — `.global` exports, `.extern` imports, nothing more
  ([Project_specs.md:77-80](Project_specs.md#L77-L80)). GNU `as` ignores `.extern`
  entirely. So this is my call to make.
- It matters because the locked relocation rule branches on exactly this bit:
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
- Once decided, move the resolution to `assumptions.md`.
- Introduce Byte for std::uint8_t and Word for std::uint32_t and refactor the code so it uses the new types, for readability

## `.equ` symbols should not go through the pool as relocations

- `getOrCreatePoolEntry` makes a `SYMBOL` entry for every `ImmediateSymbol` /
  `MemorySymbol` operand ([assembler.cpp](src/asembler/assembler.cpp)),
  and `resolveRelocs` skips symbols with `section_index == ABS`. So an absolute
  symbol currently ships as an `R_32` against a symbol with no section, with a
  zero word in the pool — a case the linker would have to invent a rule for.
- An `.equ` value is known at assembly time. It should become a `LITERAL` entry
  holding that value, with no relocation at all, exactly like a wide literal.
- Not live yet: `.equ` is still a stub ([parser.y](misc/parser.y)).
  Fix it when level C lands, before the linker learns any ABS handling.
- Note the dedup consequence: an `.equ` symbol and a literal of the same value
  then share one slot, which is correct and free.

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
