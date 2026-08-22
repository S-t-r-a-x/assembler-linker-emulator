#include "../../inc/assembler.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

// Cosmetic, but must stay fixed so re-emitting a parsed object file reproduces it exactly.
static constexpr std::size_t BYTES_PER_LINE = 16;

static std::string toHex(std::uint32_t value, int width) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(width) << value;
    return stream.str();
}

SymIndex Assembler::findSymbolByName(const std::string &entry_name) const {
    for (const SymTab_entry& entry : symbol_table) {
        if (entry.name == entry_name)
            return entry.index;
    }
    return UND;
}

SymIndex Assembler::getOrCreateSymbol(const std::string &symbol_name) {
    SymIndex index = findSymbolByName(symbol_name);
    if (!index) {
        // no entry found, have to create one
        index = symbol_table.size();
        symbol_table.push_back(SymTab_entry(index, symbol_name));
    }
    return index;
}

Section *Assembler::findSection(SymIndex symTab_index) {
    for (Section &section : section_table) {
        if (section.symTab_index == symTab_index)
            return &section;
    }
    return nullptr; // no section with that symbol
}

void Assembler::openSection(const std::string &section_name) {
    SymIndex index = getOrCreateSymbol(section_name);
    // setup section in symbol table
    SymTab_entry &sectionEntry = symbol_table[index];
    sectionEntry.value = 0;
    sectionEntry.type = Type::SCTN;
    sectionEntry.bind = Bind::LOC;
    sectionEntry.section_index = index;
    sectionEntry.is_defined = true;
    // setup section in section table
    if (findSection(index) == nullptr)
        section_table.push_back(Section(index));
    current_section = index;
}

void Assembler::declareGlobal(const std::string &symbol_name) {
    // TODO maybe, add guard check for SCTN type
    symbol_table[getOrCreateSymbol(symbol_name)].bind = Bind::GLOB;
}

void Assembler::declareExtern(const std::string &symbol_name) {
    // TODO maybe, add guard check for SCTN type
    SymIndex index = getOrCreateSymbol(symbol_name);
    SymTab_entry &symbol_entry = symbol_table[index];
    symbol_entry.bind = Bind::GLOB;
    symbol_entry.is_extern = true;
}

void Assembler::defineLabel(const std::string &label_name) {
    if (current_section == NO_SECTION) {
        error("Label outside of any section: " + label_name);
        return;
    }

    SymIndex index = getOrCreateSymbol(label_name);
    SymTab_entry &label_entry = symbol_table[index];
    
    if(label_entry.is_defined) {
        error("Multiple label definitions. Label: " + label_name);
        return;
    }
    
    label_entry.value = findSection(current_section)->getLocationCounter();
    label_entry.section_index = current_section;
    label_entry.is_defined = true;
}

void Assembler::error(const std::string &error_msg) {
    error_count++;
    std::cerr << "Error: " << error_msg << std::endl;
}

void Assembler::emitByte(std::uint8_t byte) {
    Section *section = findSection(current_section);
    if(section == nullptr) {
        error("No section to emit byte into!");
        return;
    }

    section->bytes.push_back(byte);
}

void Assembler::emitWord(std::uint32_t word) {
    Section *section = findSection(current_section);
    if (section == nullptr) {
        error("No section to emit word into!");
        return;
    }

    std::uint8_t byte = word;
    for(int i = 0; i < 4; i++) {
        section->bytes.push_back(byte);
        word >>= 8;
        byte = word;
    }
}

void Assembler::emitInstruction(std::uint8_t oc,
                                std::uint8_t mod,
                                RegIndex regA,
                                RegIndex regB,
                                RegIndex regC,
                                Displacement disp) {
    std::uint32_t instruction_word = 
      (std::uint32_t(oc & 0xF) << 28) 
    | (std::uint32_t(mod & 0xF) << 24) 
    | (std::uint32_t(regA & 0xF) << 20) 
    | (std::uint32_t(regB & 0xF) << 16) 
    | (std::uint32_t(regC & 0xF) << 12) 
    | (std::uint32_t(disp) & 0xFFF);

    Section *section = findSection(current_section);
    if (section == nullptr) {
        error("No section to emit instruction into!");
        return;
    }

    std::uint8_t instruction_byte;
    for (int i = 0; i < 4; i++) {
        instruction_byte = instruction_word >> (32 - 8*(i+1));
        section->bytes.push_back(instruction_byte);
    }
}

void Assembler::instructionNoOperand(const std::string &mnemonic) {
    if (mnemonic == "halt")
        emitInstruction(0x0, 0x0, 0, 0, 0, 0);
    else if (mnemonic == "int")
        emitInstruction(0x1, 0x0, 0, 0, 0, 0);
    else if (mnemonic == "ret")
        // pop pc
        emitInstruction(0x9, 0x3, PC, SP, 0, 4);
    else if (mnemonic == "iret") {
        // pop pc -> pop status cannot be done in that order because restoring pc first
        // would jump to a different instruction before pop status runs. Implemented to status out of
        // [sp + 4] without touching sp, then let the pc load pop both words at once.
        emitInstruction(0x9, 0x6, CSR_STATUS, SP, 0, 4); // csr[A] <= mem32[gpr[B] + gpr[C] + D]
        emitInstruction(0x9, 0x3, PC, SP, 0, 8);         // gpr[A] <= mem32[gpr[B]]; gpr[B] += D
    }
    else
        error("unknown instruction: " + mnemonic);
}

void Assembler::instructionOneRegister(const std::string &mnemonic, RegIndex reg) {
    if (mnemonic == "push")
        // sp <= sp - 4; mem32[sp] <= reg, which is st with MOD 0001
        emitInstruction(0x8, 0x1, SP, 0, reg, -4);
    else if (mnemonic == "pop")
        // reg <= mem32[sp]; sp <= sp + 4, which is ld with MOD 0011
        emitInstruction(0x9, 0x3, reg, SP, 0, 4);
    else if (mnemonic == "not")
        emitInstruction(0x6, 0x0, reg, reg, 0, 0);
    else
        error("unknown instruction, or wrong operands for it: " + mnemonic);
}

void Assembler::instructionTwoRegisters(const std::string &mnemonic, RegIndex src, RegIndex dst) {
    // Format for all of these is "dst <= dst op src" in assembly
    // gpr[A] <= gpr[B] op gpr[C], which means that A and B are both dst, and C is src.
    if (mnemonic == "add")
        emitInstruction(0x5, 0x0, dst, dst, src, 0);
    else if (mnemonic == "sub")
        emitInstruction(0x5, 0x1, dst, dst, src, 0);
    else if (mnemonic == "mul")
        emitInstruction(0x5, 0x2, dst, dst, src, 0);
    else if (mnemonic == "div")
        emitInstruction(0x5, 0x3, dst, dst, src, 0);
    else if (mnemonic == "and")
        emitInstruction(0x6, 0x1, dst, dst, src, 0);
    else if (mnemonic == "or")
        emitInstruction(0x6, 0x2, dst, dst, src, 0);
    else if (mnemonic == "xor")
        emitInstruction(0x6, 0x3, dst, dst, src, 0);
    else if (mnemonic == "shl")
        emitInstruction(0x7, 0x0, dst, dst, src, 0);
    else if (mnemonic == "shr")
        emitInstruction(0x7, 0x1, dst, dst, src, 0);
    else if (mnemonic == "xchg")
        emitInstruction(0x4, 0x0, 0, src, dst, 0);
    else if (mnemonic == "ld" || mnemonic == "st")
        // ld %rS, %rD and st %rS, %rD are the same token shape as add %rS, %rD, so they
        // land here. Both mean dst <= src: gpr[A] <= gpr[B] + 0.
        emitInstruction(0x9, 0x1, dst, src, 0, 0);
    else
        error("unknown instruction, or wrong operands for it: " + mnemonic);
}

void Assembler::instructionCsrRead(const std::string &mnemonic, RegIndex csr, RegIndex gpr) {
    if (mnemonic != "csrrd") {
        error("unknown instruction, or wrong operands for it: " + mnemonic);
        return;
    }

    // ld with MOD 0000: gpr[A] <= csr[B]
    emitInstruction(0x9, 0x0, gpr, csr, 0, 0);
}

void Assembler::instructionCsrWrite(const std::string &mnemonic, RegIndex gpr, RegIndex csr) {
    if (mnemonic != "csrwr") {
        error("unknown instruction, or wrong operands for it: " + mnemonic);
        return;
    }

    // ld with MOD 0100: csr[A] <= gpr[B]
    emitInstruction(0x9, 0x4, csr, gpr, 0, 0);
}

void Assembler::emitZeros(std::uint32_t number_of_zeros) {
    Section *section = findSection(current_section);
    if (section == nullptr) {
        error("No section to emit zeroes into!");
        return;
    }

    for(std::uint32_t i = 0; i < number_of_zeros; i++)
        section->bytes.push_back(0);
}

void Assembler::relocateAgainst(SymIndex symbol_index) {
    Section *section = findSection(current_section);
    if (section == nullptr) {
        error("No section to record a relocation in!");
        return;
    }

    // Local vs extern is decided in resolveRelocs(), not here.
    Reloc reloc;
    reloc.offset = section->getLocationCounter(); // the hole starts where we are now
    reloc.symbol = symbol_index;
    reloc.addend = 0;

    section->relocs.push_back(reloc);
}

void Assembler::resolveRelocs() {
    for (Section &section : section_table) {
        for (Reloc &reloc : section.relocs) {
            const SymTab_entry &symbol = symbol_table[reloc.symbol];

            // Extern survives linking, absolute has no section: both stay as they are.
            // If symbol is in the same file, then S is the section symbol which holds it, and its value will ultimately be the sections base address
            // Otherwise, if the symbol that we need is extern, A = 0 and S will be the full resolved address of that symbol from the other file
            if (symbol.section_index == UND || symbol.section_index == ABS)
                continue;

            reloc.addend = symbol.value;
            reloc.symbol = symbol.section_index;
        }
    }
}

void Assembler::handleWordSymbol(const std::string &symbol_name) {
    if (current_section == NO_SECTION) {
        error("Symbol outside of any section: " + symbol_name);
        return;
    }

    SymIndex index = getOrCreateSymbol(symbol_name);
    relocateAgainst(index); // must come first, it reads the location counter
    emitWord(0);            
}

void Assembler::handleWordLiteral(const std::uint32_t literal) {
    emitWord(literal);
}

void Assembler::endAssembly() {
    if (hadError())
        return;

    resolveRelocs();
    writeObjectFile();
}

void Assembler::writeObjectFile() {
    std::ofstream out(write_path);
    if (!out) {
        error("cannot open output file: " + write_path);
        return;
    }

    // Symbol 0 is the null entry, manually write it out
    out << "SYMTAB " << symbol_table.size() - 1 << "\n";
    for (const SymTab_entry &entry : symbol_table) {
        if (entry.index == UND)
            continue;

        out << entry.index << " "
            << toHex(entry.value, 8) << " "
            << (entry.type == Type::SCTN ? "SCTN" : "NOTYP") << " "
            << (entry.bind == Bind::GLOB ? "GLOB" : "LOC") << " "
            << entry.section_index << " "
            << entry.name << "\n";
    }

    for (const Section &section : section_table) {
        out << "\nSECTION " << symbol_table[section.symTab_index].name
            << " " << section.bytes.size() << "\n";

        for (std::size_t i = 0; i < section.bytes.size(); i++) {
            out << toHex(section.bytes[i], 2);
            bool end_of_line = (i + 1) % BYTES_PER_LINE == 0;
            bool last_byte = i + 1 == section.bytes.size();
            out << (end_of_line || last_byte ? "\n" : " ");
        }

        if (section.relocs.empty())
            continue;

        out << "\nRELOC " << symbol_table[section.symTab_index].name
            << " " << section.relocs.size() << "\n";

        for (const Reloc &reloc : section.relocs) {
            out << toHex(reloc.offset, 8) << " "
                << reloc.symbol << " "
                << toHex(reloc.addend, 8) << "\n";
        }
    }
}

static const char *operandKindName(InstructionOperandKind kind) {
    switch (kind) {
        case InstructionOperandKind::ImmediateLiteral: return "$<literal>";
        case InstructionOperandKind::ImmediateSymbol:  return "$<symbol>";
        case InstructionOperandKind::MemoryLiteral:    return "<literal>";
        case InstructionOperandKind::MemorySymbol:     return "<symbol>";
        case InstructionOperandKind::RegisterDirect:   return "%<reg>";
        case InstructionOperandKind::RegisterIndirect: return "[%<reg>]";
        case InstructionOperandKind::RegisterLiteralOffset:  return "[%<reg> + <literal>]";
        case InstructionOperandKind::RegisterSymbolOffset:   return "[%<reg> + <symbol>]";
    }
    return "<unknown>";
}

bool Assembler::displacementFromSymbol(const std::string &symbol_name, Displacement &disp) {
    SymIndex index = findSymbolByName(symbol_name);

    if (index == UND || !symbol_table[index].is_defined) {
        error("final value of " + symbol_name + " is not known at assembly time");
        return false;
    }
    if (symbol_table[index].section_index != ABS) {
        // Only a symbol produced by .equ has a value known at assembly time
        error(symbol_name + " is not an absolute symbol, so it cannot be a displacement");
        return false;
    }

    disp = static_cast<Displacement>(symbol_table[index].value);
    if (!fitsInDisplacement(disp)) {
        error("value of " + symbol_name + " does not fit in a 12 bit signed displacement");
        return false;
    }
    return true;
}

void Assembler::instructionLoad(const std::string &mnemonic,
                                const InstructionOperand &operand,
                                RegIndex gpr) {
    if (mnemonic != "ld") {
        error("unknown instruction, or wrong operands for it: " + mnemonic);
        return;
    }

    Displacement disp = static_cast<Displacement>(operand.literal);

    switch (operand.kind) {
        case InstructionOperandKind::ImmediateLiteral:
            if (!fitsInDisplacement(disp)) {
                error("ld $<literal> too wide for a displacement, needs the literal pool");
                return;
            }
            // gpr[A] <= gpr[B] + D, with B = r0
            emitInstruction(0x9, 0x1, gpr, 0, 0, disp);
            break;

        case InstructionOperandKind::MemoryLiteral:
            if (!fitsInDisplacement(disp)) {
                error("ld <literal> address too wide for a displacement, needs the literal pool");
                return;
            }
            // gpr[A] <= mem32[gpr[B] + gpr[C] + D], with B = C = r0
            emitInstruction(0x9, 0x2, gpr, 0, 0, disp);
            break;

        case InstructionOperandKind::RegisterIndirect:
            emitInstruction(0x9, 0x2, gpr, operand.reg, 0, 0);
            break;

        case InstructionOperandKind::RegisterLiteralOffset:
            if (!fitsInDisplacement(disp)) {
                error("ld [%reg + <literal>]: literal does not fit in a 12 bit signed displacement");
                return;
            }
            emitInstruction(0x9, 0x2, gpr, operand.reg, 0, disp);
            break;

        case InstructionOperandKind::RegisterSymbolOffset:
            if (!displacementFromSymbol(operand.symbol, disp))
                return;
            emitInstruction(0x9, 0x2, gpr, operand.reg, 0, disp);
            break;

        default:
            instructionUnimplemented(mnemonic, operand);
            break;
    }
}

void Assembler::instructionStore(const std::string &mnemonic,
                                 RegIndex gpr,
                                 const InstructionOperand &operand) {
    if (mnemonic != "st") {
        error("unknown instruction, or wrong operands for it: " + mnemonic);
        return;
    }

    Displacement disp = static_cast<Displacement>(operand.literal);

    switch (operand.kind) {
        case InstructionOperandKind::ImmediateLiteral:
        case InstructionOperandKind::ImmediateSymbol:
            error("st cannot store into an immediate operand");
            return;

        case InstructionOperandKind::MemoryLiteral:
            if (!fitsInDisplacement(disp)) {
                error("st <literal> address too wide for a displacement, needs the literal pool");
                return;
            }
            // mem32[gpr[A] + gpr[B] + D] <= gpr[C], with A = B = r0
            emitInstruction(0x8, 0x0, 0, 0, gpr, disp);
            break;

        case InstructionOperandKind::RegisterIndirect:
            emitInstruction(0x8, 0x0, operand.reg, 0, gpr, 0);
            break;

        case InstructionOperandKind::RegisterLiteralOffset:
            if (!fitsInDisplacement(disp)) {
                error("st [%reg + <literal>]: literal does not fit in a 12 bit signed displacement");
                return;
            }
            emitInstruction(0x8, 0x0, operand.reg, 0, gpr, disp);
            break;

        case InstructionOperandKind::RegisterSymbolOffset:
            if (!displacementFromSymbol(operand.symbol, disp))
                return;
            emitInstruction(0x8, 0x0, operand.reg, 0, gpr, disp);
            break;

        default:
            instructionUnimplemented(mnemonic, operand);
            break;
    }
}

void Assembler::instructionUnimplemented(const std::string &mnemonic,
                                         const InstructionOperand &operand) {
    error(mnemonic + ": operand notation " + operandKindName(operand.kind)
          + " is parsed but not encoded yet");
}
