#include <iostream>
#include "../../inc/assembler.hpp"

void Assembler::addToSymbolTable(SymTab_entry entry) {
        symTab.push_back(entry);
}

void Assembler::openSection(std::string sectionName) {
    unsigned short index = symTab.size();
    addToSymbolTable({index, 0x0, Type::SCTN, Bind::LOC, index, sectionName});
    currentSection = index;
    std::cout << "Opening directive!" << std::endl; 
}
