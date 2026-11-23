#pragma once

#include <array>
#include <chrono>
#include <unordered_map>
#include <vector>

#include "hexfile.hpp"
#include "isa.hpp"
#include "terminal.hpp"

// Both a cold and a warm restart start here, which is why every public test
// places its code section at exactly this address.
constexpr Word RESET_ADDRESS = 0x40000000;

// Flags in the low bits of the status register. A set bit masks, a clear bit
// enables, which is the opposite way round from how the names read.
constexpr Word STATUS_TIMER_MASKED = 0x1;
constexpr Word STATUS_TERMINAL_MASKED = 0x2;
constexpr Word STATUS_INTERRUPTS_MASKED = 0x4;

// What the handler finds in the cause register.
enum class Cause : Word {
    BadInstruction = 1,
    Timer = 2,
    Terminal = 3,
    Software = 4
};

// The peripheral registers, which live in the address space rather than in the
// processor, so every data access has to look for them first.
constexpr Word TERM_OUT = 0xFFFFFF00;
constexpr Word TERM_IN = 0xFFFFFF04;
constexpr Word TIM_CFG = 0xFFFFFF10;

// The OC field. MOD picks between the variants within one of these, and is
// spelled as the bit pattern the spec gives it at each use site.
enum Opcode : Byte {
    OC_HALT = 0x0,
    OC_INT = 0x1,
    OC_CALL = 0x2,
    OC_JUMP = 0x3,
    OC_XCHG = 0x4,
    OC_ARITHMETIC = 0x5,
    OC_LOGIC = 0x6,
    OC_SHIFT = 0x7,
    OC_STORE = 0x8,
    OC_LOAD = 0x9
};

// 2^32 bytes of address space, though a program only ever touches a handful of
// pages. A page is allocated on first write; anything untouched reads back as zero.
class Memory {
public:
    Byte readByte(Word address) const;
    void writeByte(Word address, Byte value);

    // Data is little-endian. Instructions are not, and are fetched byte by byte.
    Word readWord(Word address) const;
    void writeWord(Word address, Word value);

private:
    static constexpr Word PAGE_SIZE = 4096;
    std::unordered_map<Word, std::vector<Byte>> pages;
};

// One decoded instruction word. Which fields carry meaning depends on oc and mod.
struct Instruction {
    Byte oc = 0;
    Byte mod = 0;
    RegIndex regA = 0;
    RegIndex regB = 0;
    RegIndex regC = 0;
    Displacement disp = 0;
};

class Emulator {
public:
    explicit Emulator(const hexfile::Image &image);

    // Runs until the program executes halt, then prints the processor state.
    void run();

private:
    std::array<Word, 16> gpr{};
    std::array<Word, CSR_COUNT> csr{};
    Memory memory;
    Terminal terminal;
    bool halted = false;

    // A peripheral's request line: set when it wants attention, cleared once
    // served. Just one bit, so a second request arriving before the first is
    // served gets lost rather than queued.
    bool timer_pending = false;
    bool terminal_pending = false;

    Word term_in = 0;
    Word tim_cfg = 0;
    std::chrono::steady_clock::time_point timer_deadline;

    // r0 is hardwired to zero, so a write naming it is discarded.
    void setGpr(RegIndex reg, Word value);

    // Data accesses, which see the peripheral registers. Instruction fetch does not.
    Word loadWord(Word address);
    void storeWord(Word address, Word value);
    void push(Word value);

    Instruction fetch();

    // False for any encoding the ISA gives no meaning to; run() turns that
    // into a cause-1 interrupt.
    bool execute(const Instruction &instruction);
    bool executeJump(const Instruction &instruction);
    bool executeAlu(const Instruction &instruction);
    bool executeStore(const Instruction &instruction);
    bool executeLoad(const Instruction &instruction);

    void enterInterrupt(Cause cause);
    void serviceInterrupts();

    std::chrono::milliseconds timerPeriod() const;
    bool timerExpired();

    void printState() const;
};
