# The tests in this directory, in the same shape as the start.sh scripts that
# ship with tests/javni_testovi.
cd "$(dirname "$0")"
BIN=../bin

${BIN}/asembler -o emulator_isa.o emulator_isa.s
${BIN}/linker -hex -place=test_code@0x40000000 -o emulator_isa.hex emulator_isa.o
${BIN}/emulator emulator_isa.hex

${BIN}/asembler -o emulator_traps.o emulator_traps.s
${BIN}/linker -hex -place=trap_code@0x40000000 -o emulator_traps.hex emulator_traps.o
${BIN}/emulator emulator_traps.hex

# The two halves of section lib are merged into one object first, then linked
# as an ordinary input file, so their relocations have to survive the round trip.
${BIN}/asembler -o relocatable/main.o relocatable/main.s
${BIN}/asembler -o relocatable/lib_add.o relocatable/lib_add.s
${BIN}/asembler -o relocatable/lib_mul.o relocatable/lib_mul.s
${BIN}/linker -relocatable -o relocatable/lib.o relocatable/lib_add.o relocatable/lib_mul.o
${BIN}/linker -hex -place=my_code@0x40000000 -place=lib@0x40001000 \
  -o relocatable/program.hex relocatable/main.o relocatable/lib.o
${BIN}/emulator relocatable/program.hex
