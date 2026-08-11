#pragma once

// The defense VM's g++ 9.4 defaults to gnu++14 but supports -std=c++17 fully;
// the Makefile passes the flag. Fail loudly if it ever goes missing, rather
// than with pages of template errors from std::variant below.
static_assert(__cplusplus >= 201703L,
              "This project requires C++17 -- build with -std=c++17");

#include <climits>
#include <string>
#include <vector>
#include <variant>
#include <cstdint>

enum class Type {
    NOTYP,
    SCTN
};

enum class Bind {
    LOC,
    GLOB 
};

struct SymTab_entry {
    /*  Symbol table entry */
    // Constants
    static constexpr unsigned short UND = 0;
    static constexpr unsigned short ABS = USHRT_MAX; // just gotta care not to hit this value but that shouldnt be a problem
    // Fields: Num Value Type Bind Ndx Name
    unsigned short num;       // index in the symbol table
    unsigned int value;     // offset within its section (0 for undefined or section)
    Type type;              
    Bind bind;              
    unsigned short ndx;     // num of the section it belongs to (can be UND or ABS)
    std::string name;       
};

// Directive relevant
using Operand = std::variant<std::uint32_t, std::string>;
using OperandList = std::vector<Operand>;

class Assembler {
public:
    Assembler(std::string writePath) {
        this->writePath = writePath;

        // first entry in the symbol table - null entry, represents no symbol
        addToSymbolTable({0, 0x0, Type::NOTYP, Bind::LOC, SymTab_entry::UND, ""});
        currentSection = SymTab_entry::UND;
    }

    void addToSymbolTable(SymTab_entry entry);


    void openSection(std::string sectionName);
    
private:
    std::vector<SymTab_entry> symTab;
    unsigned short currentSection; // where we are now, for adding symbols
    std::string writePath;
};
