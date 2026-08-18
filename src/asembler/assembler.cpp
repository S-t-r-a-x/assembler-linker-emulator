#include "../../inc/assembler.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

// Bytes per line in a SECTION block. Purely cosmetic - the reader tokenizes
// with >>, so line breaks are whitespace to it - but it has to be a fixed rule
// so that re-emitting a parsed object file reproduces it byte for byte.
static constexpr std::size_t BYTES_PER_LINE = 16;

// Fixed-width lowercase hex, so columns line up when eyeballing an object file.
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

    // Only record what was referenced. Whether the symbol is local or extern is not knowable yet, since a forward reference still looks undefined as we pass it, so resolveRelocs() makes that call at .end instead.
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

            // Extern keeps pointing at the symbol itself, since that one does survive linking. 
            // An absolute symbol has no section to point at.
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