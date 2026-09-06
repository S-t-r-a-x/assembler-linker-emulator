# The two emulator tests in this directory, in the same shape as the start.sh
# scripts that ship with tests/javni_testovi.
cd "$(dirname "$0")"
BIN=../bin

${BIN}/asembler -o emulator_isa.o emulator_isa.s
${BIN}/linker -hex -place=test_code@0x40000000 -o emulator_isa.hex emulator_isa.o
${BIN}/emulator emulator_isa.hex

${BIN}/asembler -o emulator_traps.o emulator_traps.s
${BIN}/linker -hex -place=trap_code@0x40000000 -o emulator_traps.hex emulator_traps.o
${BIN}/emulator emulator_traps.hex
