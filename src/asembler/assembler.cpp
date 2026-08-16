#include "../../inc/assembler.hpp"
#include <iostream>

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