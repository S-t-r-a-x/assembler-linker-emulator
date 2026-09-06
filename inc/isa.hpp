#pragma once

// Facts about the target ISA that both the assembler and the emulator have to
// agree on: one encodes them, the other executes them, and a disagreement
// shows up only as a user program running wrong. So they live in one place.

// The defense VM's g++ 9.4 defaults to gnu++14 but supports -std=c++17 fully;
// the Makefile passes the flag. Fail loudly if it ever goes missing, rather
// than with pages of template errors further down the include chain.
static_assert(__cplusplus >= 201703L,
              "This project requires C++17, build with -std=c++17");

#include <cstdint>

using Byte = std::uint8_t;
using Word = std::uint32_t;         // a machine word, and an address

using RegIndex = std::uint8_t;      // 0..15
using Displacement = std::int32_t;  // SIGNED

// The two general purpose registers with a second name in the ISA.
// Stack grows downwards, and SP points ot the adress of the occupied adress at the top of the stack
constexpr RegIndex SP = 14;
constexpr RegIndex PC = 15;

// The control and status registers are indexed apart from the general purpose ones.
constexpr RegIndex CSR_STATUS = 0;
constexpr RegIndex CSR_HANDLER = 1;
constexpr RegIndex CSR_CAUSE = 2;
constexpr unsigned CSR_COUNT = 3;

constexpr Word INSTRUCTION_SIZE = 4;

// Cast unsigned values with static_cast<Displacement> first: the field is sign
// extended when executed, so 0xFFFFFEFE is -258 and does fit.
constexpr Displacement DISPLACEMENT_MIN = -2048;
constexpr Displacement DISPLACEMENT_MAX = 2047;

constexpr bool fitsInDisplacement(Displacement disp) {
    return disp >= DISPLACEMENT_MIN && disp <= DISPLACEMENT_MAX;
}

// The other direction: the 12 bit field as the processor reads it. Written as a
// subtraction rather than a cast of a widened unsigned, which is only
// implementation defined before C++20.
constexpr Displacement signExtendDisplacement(Word field) {
    Word value = field & 0xFFF;
    return static_cast<Displacement>(value) - ((value & 0x800) ? 0x1000 : 0);
}

// PC is incremented before execute, so a pc-relative field is measured from the
// instruction after this one, so we need to decrement the displacement by another word
// Both arguments are offsets within the same section, or both addresses
constexpr Displacement pcRelativeDisplacement(Word target, Word instruction) {
    return static_cast<Displacement>(target) - static_cast<Displacement>(instruction)
         - static_cast<Displacement>(INSTRUCTION_SIZE);
}
