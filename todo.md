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
