#include "../../inc/assembler.hpp"

unsigned short Assembler::findByName(const std::string &entryName) const {
    for (const SymTab_entry& entry : symTab) {
        if (entry.name == entryName)
            return entry.num;
    }
    return 0;
}

unsigned short Assembler::getOrCreateSymbol(const std::string &symbolName) {
    unsigned short num = findByName(symbolName);
    if (!num) {
        // no entry found, have to create one
        num = symTab.size();
        symTab.push_back(SymTab_entry(num, symbolName));
    }
    return num;
}

void Assembler::openSection(const std::string &sectionName) {
    unsigned short num = getOrCreateSymbol(sectionName);
    // setup section
    SymTab_entry &sectionEntry = symTab[num];
    sectionEntry.value = 0;
    sectionEntry.type = Type::SCTN;
    sectionEntry.bind = Bind::LOC;
    sectionEntry.ndx = num;
    // TODO - reset location counter - here or in getOrCreateSymbol
    // set current section - important
    currentSection = num;
}

void Assembler::declareGlobal(const std::string &symbolName) {
    // TODO maybe, add guard check for SCTN type
    symTab[getOrCreateSymbol(symbolName)].bind = Bind::GLOB;
}

void Assembler::declareExtern(const std::string &symbolName) {
    // TODO maybe, add guard check for SCTN type
    symTab[getOrCreateSymbol(symbolName)].bind = Bind::GLOB;
}
