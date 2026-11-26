#include "../../inc/emulator.hpp"
#include <iomanip>
#include <iostream>

Byte Memory::readByte(Word address) const {
    auto page = pages.find(address / PAGE_SIZE);
    if (page == pages.end())
        return 0;
    return page->second[address % PAGE_SIZE];
}

void Memory::writeByte(Word address, Byte value) {
    std::vector<Byte> &page = pages[address / PAGE_SIZE];
    if (page.empty())
        page.resize(PAGE_SIZE);
    page[address % PAGE_SIZE] = value;
}

Word Memory::readWord(Word address) const {
    Word value = 0;
    for (unsigned i = 0; i < 4; i++)
        value |= Word(readByte(address + i)) << (8 * i);
    return value;
}

void Memory::writeWord(Word address, Word value) {
    for (unsigned i = 0; i < 4; i++)
        writeByte(address + i, Byte(value >> (8 * i)));
}

Emulator::Emulator(const hexfile::Image &image) {
    for (const auto &[address, byte] : image)
        memory.writeByte(address, byte);

    gpr[PC] = RESET_ADDRESS;
    timer_deadline = std::chrono::steady_clock::now() + timerPeriod();
}

void Emulator::setGpr(RegIndex reg, Word value) {
    if (reg != 0)
        gpr[reg] = value;
}

Word Emulator::loadWord(Word address) {
    switch (address) {
        case TERM_IN: return term_in;
        case TIM_CFG: return tim_cfg;
        // term_out is write-only: the terminal consumes what is written and
        // keeps nothing to read back.
        default: return memory.readWord(address);
    }
}

void Emulator::storeWord(Word address, Word value) {
    switch (address) {
        case TERM_OUT:
            // The register is a word wide, a character is its low byte.
            terminal.writeChar(Byte(value));
            return;
        case TERM_IN:
            term_in = value;
            return;
        case TIM_CFG:
            tim_cfg = value;
            // Reconfiguring restarts the period rather than reinterpreting how
            // much of the old one had already elapsed.
            timer_deadline = std::chrono::steady_clock::now() + timerPeriod();
            return;
        default:
            memory.writeWord(address, value);
    }
}

void Emulator::push(Word value) {
    gpr[SP] -= sizeof(Word);
    storeWord(gpr[SP], value);
}

// Instructions are laid out I.II.III.IV in ascending address order and decoded
// field by field, which is why this reads bytes rather than a little-endian word.
Instruction Emulator::fetch() {
    const Word address = gpr[PC];
    gpr[PC] += INSTRUCTION_SIZE;

    const Byte first = memory.readByte(address);
    const Byte second = memory.readByte(address + 1);
    const Byte third = memory.readByte(address + 2);
    const Byte fourth = memory.readByte(address + 3);

    Instruction instruction;
    instruction.oc = first >> 4;
    instruction.mod = first & 0xF;
    instruction.regA = second >> 4;
    instruction.regB = second & 0xF;
    instruction.regC = third >> 4;
    instruction.disp = signExtendDisplacement((Word(third & 0xF) << 8) | fourth);
    return instruction;
}

bool Emulator::execute(const Instruction &instruction) {
    switch (instruction.oc) {
        case OC_HALT:
            if (instruction.mod != 0)
                return false;
            halted = true;
            return true;

        case OC_INT:
            if (instruction.mod != 0)
                return false;
            enterInterrupt(Cause::Software);
            return true;

        case OC_CALL: {
            if (instruction.mod > 1)
                return false;
            Word target = gpr[instruction.regA] + gpr[instruction.regB] + Word(instruction.disp);
            if (instruction.mod == 1)
                target = loadWord(target);
            // pc was incremented at fetch, so this is already the return address
            push(gpr[PC]);
            gpr[PC] = target;
            return true;
        }

        case OC_JUMP:
            return executeJump(instruction);

        case OC_XCHG: {
            if (instruction.mod != 0)
                return false;
            const Word temp = gpr[instruction.regB];
            setGpr(instruction.regB, gpr[instruction.regC]);
            setGpr(instruction.regC, temp);
            return true;
        }

        case OC_ARITHMETIC:
        case OC_LOGIC:
        case OC_SHIFT:
            return executeAlu(instruction);

        case OC_STORE:
            return executeStore(instruction);

        case OC_LOAD:
            return executeLoad(instruction);
    }
    return false;
}

bool Emulator::executeJump(const Instruction &instruction) {
    // MOD is a condition in its low bits plus one bit saying whether the target
    // is the computed address itself or the word stored there.
    bool taken = false;
    switch (instruction.mod & 0x7) {
        case 0x0: taken = true; break;
        case 0x1: taken = gpr[instruction.regB] == gpr[instruction.regC]; break;
        case 0x2: taken = gpr[instruction.regB] != gpr[instruction.regC]; break;
        case 0x3: taken = std::int32_t(gpr[instruction.regB]) > std::int32_t(gpr[instruction.regC]); break;
        default: return false;
    }
    if (!taken)
        return true;    // an untaken branch reads nothing, not even indirectly

    const Word target = gpr[instruction.regA] + Word(instruction.disp);
    gpr[PC] = (instruction.mod & 0x8) ? loadWord(target) : target;
    return true;
}

// Arithmetic, logic and shift all share one shape, gpr[A] <= gpr[B] op gpr[C];
// only the operation itself differs.
bool Emulator::executeAlu(const Instruction &instruction) {
    const Word left = gpr[instruction.regB];
    const Word right = gpr[instruction.regC];
    Word result = 0;

    switch (instruction.oc) {
        case OC_ARITHMETIC:
            switch (instruction.mod) {
                case 0x0: result = left + right; break;
                case 0x1: result = left - right; break;
                case 0x2: result = left * right; break;
                // Division by zero has no value the ISA could produce, so it's
                // treated as an error.
                case 0x3:
                    if (right == 0)
                        return false;
                    result = left / right;
                    break;
                default: return false;
            }
            break;

        case OC_LOGIC:
            switch (instruction.mod) {
                case 0x0: result = ~left; break;
                case 0x1: result = left & right; break;
                case 0x2: result = left | right; break;
                case 0x3: result = left ^ right; break;
                default: return false;
            }
            break;

        case OC_SHIFT:
            // Shifting by 32 or more is undefined in C++ but has an obvious
            // meaning here: every bit is shifted out.
            if (instruction.mod == 0x0)
                result = right >= 32 ? 0 : left << right;
            else if (instruction.mod == 0x1)
                result = right >= 32 ? 0 : left >> right;
            else
                return false;
            break;

        default:
            return false;
    }

    setGpr(instruction.regA, result);
    return true;
}

bool Emulator::executeStore(const Instruction &instruction) {
    // push, and the one mode that ignores gpr[B]. The pointer moves before the
    // store, so pushing sp itself stores the already decremented value.
    if (instruction.mod == 0x1) {
        setGpr(instruction.regA, gpr[instruction.regA] + Word(instruction.disp));
        storeWord(gpr[instruction.regA], gpr[instruction.regC]);
        return true;
    }

    const Word address = gpr[instruction.regA] + gpr[instruction.regB] + Word(instruction.disp);
    switch (instruction.mod) {
        case 0x0:
            storeWord(address, gpr[instruction.regC]);
            return true;
        case 0x2:
            storeWord(loadWord(address), gpr[instruction.regC]);
            return true;
    }
    return false;
}

// One case per MOD, in the order the spec lists them. Every case naming a
// control and status register is guarded: only three exist.
bool Emulator::executeLoad(const Instruction &instruction) {
    const RegIndex regA = instruction.regA;
    const RegIndex regB = instruction.regB;
    const RegIndex regC = instruction.regC;
    const Word disp = Word(instruction.disp);

    switch (instruction.mod) {
        case 0x0:
            if (regB >= CSR_COUNT)
                return false;
            setGpr(regA, csr[regB]);
            return true;
        case 0x1:
            setGpr(regA, gpr[regB] + disp);
            return true;
        case 0x2:
            setGpr(regA, loadWord(gpr[regB] + gpr[regC] + disp));
            return true;
        // pop: the two steps run in the order the spec writes them, so naming
        // the same register for A and B increments the loaded value
        case 0x3:
            setGpr(regA, loadWord(gpr[regB]));
            setGpr(regB, gpr[regB] + disp);
            return true;
        case 0x4:
            if (regA >= CSR_COUNT)
                return false;
            csr[regA] = gpr[regB];
            return true;
        case 0x5:
            if (regA >= CSR_COUNT || regB >= CSR_COUNT)
                return false;
            csr[regA] = csr[regB] | disp;
            return true;
        case 0x6:
            if (regA >= CSR_COUNT)
                return false;
            csr[regA] = loadWord(gpr[regB] + gpr[regC] + disp);
            return true;
        case 0x7:
            if (regA >= CSR_COUNT)
                return false;
            csr[regA] = loadWord(gpr[regB]);
            setGpr(regB, gpr[regB] + disp);
            return true;
    }
    return false;
}

// Pushes status then return address, then masks interrupts globally. The
// saved pc already points past the interrupted instruction, so iret resumes
// after it, even after a bad instruction that triggered a cause-1 entry.
void Emulator::enterInterrupt(Cause cause) {
    push(csr[CSR_STATUS]);
    push(gpr[PC]);
    csr[CSR_CAUSE] = Word(cause);
    csr[CSR_STATUS] |= STATUS_INTERRUPTS_MASKED;
    gpr[PC] = csr[CSR_HANDLER];
}

std::chrono::milliseconds Emulator::timerPeriod() const {
    // The register is a word wide but only eight periods exist, so the
    // selection is the low three bits.
    static constexpr unsigned periods_ms[8] = {500, 1000, 1500, 2000, 5000, 10000, 30000, 60000};
    return std::chrono::milliseconds(periods_ms[tim_cfg & 0x7]);
}

// The period restarts from the moment it elapses, so a program that leaves
// interrupts masked for a while does not get a burst of them afterwards.
bool Emulator::timerExpired() {
    const auto now = std::chrono::steady_clock::now();
    if (now < timer_deadline)
        return false;
    timer_deadline = now + timerPeriod();
    return true;
}

void Emulator::serviceInterrupts() {
    // Peripherals keep running whether or not the processor is listening: a
    // keystroke still lands in term_in, and the timer still ticks, even masked.
    if(++instructions_since_poll >= POLL_INTERVAL) {
        instructions_since_poll = 0;
        const int key = terminal.readKey();
        if (key >= 0) {
            term_in = Word(key);
            terminal_pending = true;
        }
        if (timerExpired())
            timer_pending = true;
    }

    if (csr[CSR_STATUS] & STATUS_INTERRUPTS_MASKED)
        return;

    // Only one request can be taken between two instructions; the timer is
    // offered first, arbitrarily, since the spec ranks neither above the other.
    if (timer_pending && !(csr[CSR_STATUS] & STATUS_TIMER_MASKED)) {
        timer_pending = false;
        enterInterrupt(Cause::Timer);
        return;
    }
    if (terminal_pending && !(csr[CSR_STATUS] & STATUS_TERMINAL_MASKED)) {
        terminal_pending = false;
        enterInterrupt(Cause::Terminal);
    }
}

void Emulator::run() {
    while (!halted) {
        const Instruction instruction = fetch();
        if (!execute(instruction))
            enterInterrupt(Cause::BadInstruction);

        // Requests are served only between instructions, never inside one,
        // and halt is the end of everything.
        if (!halted)
            serviceInterrupts();
    }
    printState();
}

void Emulator::printState() const {
    std::cout << "Emulated processor executed halt instruction\n"
              << "Emulated processor state:\n";

    for (unsigned reg = 0; reg < gpr.size(); reg++) {
        std::cout << "r" << std::dec << reg
                  << "=0x" << std::hex << std::setw(8) << std::setfill('0') << gpr[reg]
                  << (reg % 4 == 3 ? "\n" : " ");
    }
    std::cout << std::dec << std::flush;
}
