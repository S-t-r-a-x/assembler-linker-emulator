#include "../../inc/assembler.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

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

void Assembler::makeJumpFixup(SymIndex symbol) {
    Section *cur_sctn = findSection(current_section);
    if (cur_sctn == nullptr) {
        error("No section to create jump fixup for.");
        return;
    }
    cur_sctn->jump_fixups.push_back({cur_sctn->getLocationCounter(), symbol});
}

void Assembler::instructionOneOperand(const std::string &mnemonic, const InstructionOperand &operand) {
    if (mnemonic == "jmp")
        emitJumpInstruction(0x3, 0x0, 0, 0, operand);
    else if (mnemonic == "call")
        emitJumpInstruction(0x2, 0x0, 0, 0, operand);
    else
        error("unknown instruction: " + mnemonic);
}

void Assembler::instructionConditionalBranch(const std::string &mnemonic,
                                             RegIndex reg1,
                                             RegIndex reg2,
                                             const InstructionOperand &operand) {
    // the condition only, emitJumpInstruction applies the direct/indirect form bit
    std::uint8_t cond;
    if (mnemonic == "beq")      cond = 0b001;
    else if (mnemonic == "bne") cond = 0b010;
    else if (mnemonic == "bgt") cond = 0b011;
    else {
        error("unknown instruction, expected conditional branch!");
        return;
    }

    // if (gpr[B] cond gpr[C]) pc <= operand
    emitJumpInstruction(0x3, cond, reg1, reg2, operand);
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

void Assembler::handleAscii(const std::string &str) {
    for(std::size_t i = 0; i < str.length(); i++) {
        if (str[i] != '\\') {
            emitByte(static_cast<std::uint8_t>(str[i]));
            continue;
        }

        // str[i] is \ so the next char decides what byte is emitted
        if (i + 1 >= str.length()) {
            error("string literal ends with a dangling backslash");
            break;
        }
        i++;
        switch (str[i]) {
            case 'n':  emitByte(0x0A); break;
            case 't':  emitByte(0x09); break;
            case 'r':  emitByte(0x0D); break;
            case '\\': emitByte(0x5C); break;
            case '"':  emitByte(0x22); break;
            // GNU as emits an unrecognized escape as the character itself
            default:   emitByte(static_cast<std::uint8_t>(str[i])); break;
            /* missing cases from GNU as: 
             * \x hex-digits
             * \ digit digit digit - An octal character code 
            */
        }
    }
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

void Assembler::makePoolFixup(std::uint16_t pool_index) {
    Section *cur_sctn = findSection(current_section);
    if(cur_sctn == nullptr) {
        error("No section to create pool fixup for.");
        return;
    }
    cur_sctn->pool_fixups.push_back({cur_sctn->getLocationCounter(), pool_index});
}

void Assembler::resolveJumpFixups() {
    SymIndex last_section = current_section;
    for(Section &sctn : section_table) {
        current_section = sctn.symTab_index;
        for(JumpFixup &jf : sctn.jump_fixups) {
            SymTab_entry &sym = symbol_table[jf.symbol];
            Displacement disp_pcrel = pcRelativeDisplacement(sym.value, jf.instruction_offset); 
            if(sym.is_defined 
                && sym.section_index == sctn.symTab_index 
                && fitsInDisplacement(disp_pcrel)) {
                // instruction can use pc rel addressing
                // Patch the instruction NOW and we're done
                auto &inst_byte = jf.instruction_offset;
                // All jump instructions have a just need bit 3 of MOD cleared for direct adressing
                // call instruction needs to reset bit 1 for this
                std::uint8_t oc = sctn.bytes[inst_byte] >> 4;
                sctn.bytes[inst_byte] &= ~indirectBit(oc); 
                sctn.bytes[inst_byte + 2] = (sctn.bytes[inst_byte + 2] & 0xF0) | (disp_pcrel>>8 & 0x0F);
                sctn.bytes[inst_byte + 3] = 0xFF & disp_pcrel; 
            }
            else {
                // Need to create a pool entry for the instruction
                // Has to be done now instead of at instruction emission time, because at that point we dont know if it is necessary
                std::uint16_t slot = getOrCreatePoolEntry({InstructionOperandKind::MemorySymbol, 0, 0, sym.name});
                if (slot == NO_LITERAL_POOL_ENTRY)
                    continue;
                sctn.pool_fixups.push_back({jf.instruction_offset, slot});
            }
        }
    }
    current_section = last_section;
}

void Assembler::flushLiteralPools() {
    // Remember the current section, as we change it within the loop, restore it afterwards
    SymIndex last_section = current_section;
    for(Section &section : section_table) {
        std::uint32_t literal_pool_base = section.getLocationCounter();
        // for methods like emitByte which read current_section
        current_section = section.symTab_index;
        for(const PoolFixup &pf : section.pool_fixups) {
            // calculate displacement that will be patched into the instruction
            Displacement patch_displacement = pcRelativeDisplacement(literal_pool_base + 4*pf.pool_index, pf.instruction_offset);
            if (!fitsInDisplacement(patch_displacement)) {
                error("literal pool is too far from the instruction at offset " + std::to_string(pf.instruction_offset) + " - section: " + symbol_table[section.symTab_index].name + " is too large");
                continue;
            }
            // Patch bytes of the instruction
            // Byte3: CCCCDDDD
            section.bytes[pf.instruction_offset+2] = (section.bytes[pf.instruction_offset+2] & 0xF0) | ((patch_displacement >> 8) & 0x0F);
            // Byte4: DDDDDDDD
            section.bytes[pf.instruction_offset+3] = 0xFF & patch_displacement;
        }

        for(const LiteralPoolEntry &lp_entry : section.literal_pool) {
            if (lp_entry.is_symbol) {
                relocateAgainst(lp_entry.symbol);
                emitWord(0);
            }
            else {
                // If literal, just emit the literal value
                emitWord(lp_entry.literal);
            }
        }
    }
    current_section = last_section;
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
    resolveJumpFixups();
    flushLiteralPools();
    resolveRelocs();
    if (hadError()) return;
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

std::uint16_t Assembler::getOrCreatePoolEntry(const InstructionOperand &operand) {
    Section *cur_sect = findSection(current_section);
    if (!cur_sect) {
        error("No section to make literal pool entry for!");
        return NO_LITERAL_POOL_ENTRY;
    }

    LiteralPoolEntry pool_entry_candidate;

    switch(operand.kind) {
        case InstructionOperandKind::ImmediateLiteral:
        case InstructionOperandKind::MemoryLiteral:
            pool_entry_candidate.literal = operand.literal;
            pool_entry_candidate.is_symbol = false;
            break;
        case InstructionOperandKind::ImmediateSymbol:
        case InstructionOperandKind::MemorySymbol:
            pool_entry_candidate.symbol = getOrCreateSymbol(operand.symbol);
            pool_entry_candidate.is_symbol = true;
            break;
        default:
            error("operand kind cannot go in the literal pool");
            return NO_LITERAL_POOL_ENTRY;
    }
    // Check if literal pool entry with the necessary value exists, if not make a new one, and return its index
    auto &pool = cur_sect->literal_pool;
    auto it = std::find(pool.begin(), pool.end(), pool_entry_candidate);
    if(it != pool.end())
        return static_cast<std::uint16_t>(it - pool.begin());
    pool.push_back(pool_entry_candidate);
    return static_cast<std::uint16_t>(pool.size() - 1);
}

void Assembler::emitInstructionViaPool(std::uint8_t oc, 
                                       std::uint8_t mod, 
                                       RegIndex regA, 
                                       RegIndex regB, 
                                       RegIndex regC, 
                                       const InstructionOperand &operand) {
    std::uint16_t slot = getOrCreatePoolEntry(operand);
    if (slot == NO_LITERAL_POOL_ENTRY)
        return;
    makePoolFixup(slot);
    emitInstruction(oc, mod, regA, regB, regC, 0);
}

void Assembler::emitJumpInstruction(std::uint8_t oc,
                                    std::uint8_t cond,
                                    RegIndex regB,
                                    RegIndex regC,
                                    const InstructionOperand &operand) {
    switch (operand.kind) {
        case InstructionOperandKind::MemoryLiteral: {
            Displacement disp = static_cast<Displacement>(operand.literal);
            if (fitsInDisplacement(disp)) {
                // pc <= gpr[A] + D, with A = r0, so D is the absolute target address
                emitInstruction(oc, cond & ~indirectBit(oc), 0, regB, regC, disp);
                break;
            }
            emitInstructionViaPool(oc, cond | indirectBit(oc), PC, regB, regC, operand);
            break;
        }

        case InstructionOperandKind::MemorySymbol:
            // assume the pool is needed, resolveJumpFixups clears the bit if pc relative reaches
            makeJumpFixup(getOrCreateSymbol(operand.symbol));
            emitInstruction(oc, cond | indirectBit(oc), PC, regB, regC, 0);
            break;

        default:
            error(std::string("operand notation ") + operandKindName(operand.kind)
                  + " is not a valid jump target");
    }
}

void Assembler::instructionLoad(const std::string &mnemonic,
                                const InstructionOperand &operand,
                                RegIndex gpr) {
    if (mnemonic != "ld") {
        error("unknown instruction, or wrong operands for it: " + mnemonic);
        return;
    }

    // TODO Maybe change this naming, literal is 32 bits and displacements should be max 12 bits
    Displacement disp = static_cast<Displacement>(operand.literal);

    switch (operand.kind) {
        case InstructionOperandKind::ImmediateLiteral:
            if (!fitsInDisplacement(disp)) {
                // Literal is too wide, need to use literal pool
                // gpr[A] <= mem32[gpr[B]+gpr[C]+D]
                emitInstructionViaPool(0x9, 0x2, gpr, PC, 0, operand);
                break;
            }
            // gpr[A] <= gpr[B] + D, with B = r0
            emitInstruction(0x9, 0x1, gpr, 0, 0, disp);
            break;

        case InstructionOperandKind::MemoryLiteral:
            if (!fitsInDisplacement(disp)) {
                // Literal is too wide, needs to use literal pool
                // have to first fetch the literal
                // gpr[A] <= mem32[gpr[B] + gpr[C] + D] - gpr <= mem32[PC+0+DISP(filled later)] - pool_entry
                emitInstructionViaPool(0x9, 0x2, gpr, PC, 0, operand);
                // gpr[A] <= mem32[gpr[B] + gpr[C] + D] - gpr <= mem32[gpr(pool_entry) + 0 + 0]
                emitInstruction(0x9, 0x2, gpr, gpr, 0, 0);
                break;
            }
            // gpr[A] <= mem32[gpr[B] + gpr[C] + D], with B = C = r0
            emitInstruction(0x9, 0x2, gpr, 0, 0, disp);
            break;

        case InstructionOperandKind::ImmediateSymbol:
            // Needs literal pool entry unconditionally
            emitInstructionViaPool(0x9, 0x2, gpr, PC, 0, operand);
            break;

        case InstructionOperandKind::MemorySymbol:
            // Needs literal pool entry unconditionally
            // load symbol value into gpr
            emitInstructionViaPool(0x9, 0x2, gpr, PC, 0, operand);
            // load memory value at the address contained within the symbol value
            emitInstruction(0x9, 0x2, gpr, gpr, 0, 0);
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
                // mem32[mem32[gpr[A]+gpr[B]+D]]<=gpr[C];
                // mem32[mem32[PC + 0 + 0 (literal in literal_pool after backpatch)]] <= gpr
                emitInstructionViaPool(0x8, 0x2, PC, 0, gpr, operand);
                break;
            }
            // mem32[gpr[A] + gpr[B] + D] <= gpr[C], with A = B = r0
            emitInstruction(0x8, 0x0, 0, 0, gpr, disp);
            break;

        case InstructionOperandKind::MemorySymbol:
            // mem32[mem32[gpr[A]+gpr[B]+D]] <= gpr[C]
            // mem32[mem32[PC + 0 + 0 (will be offset to literal pool)]] <= gpr
            emitInstructionViaPool(0x8, 0x2, PC, 0, gpr, operand);
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
