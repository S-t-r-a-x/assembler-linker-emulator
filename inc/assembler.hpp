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
    // Constants, used on ndx
    static constexpr unsigned short UND = 0;
    static constexpr unsigned short ABS = USHRT_MAX; // just gotta care not to hit this value but that shouldnt be a problem
    // Fields: Num Value Type Bind Ndx Name
    unsigned short num = 0;       // index in the symbol table
    unsigned int value = 0;     // offset within its section (0 for undefined or section)
    Type type = Type::NOTYP;              
    Bind bind = Bind::LOC;              
    unsigned short ndx = UND;     // num of the section it belongs to (can be UND or ABS)
    std::string name;       

    SymTab_entry(unsigned short num, const std::string &name) {
        this->num = num;
        this->name = name;
    }
};

// Directive relevant
using Operand = std::variant<std::uint32_t, std::string>;
using OperandList = std::vector<Operand>;

class Assembler {
public:
    Assembler(std::string writePath) {
        this->writePath = writePath;

        // first entry in the symbol table - null entry, represents no symbol
        SymTab_entry &nullEntry = symTab[getOrCreateSymbol("")];
        nullEntry.value = 0;
        nullEntry.type = Type::NOTYP;
        nullEntry.bind = Bind::LOC;
        nullEntry.ndx = SymTab_entry::UND;

        currentSection = SymTab_entry::UND;
        locationCounter = 0;
    }

    void openSection(const std::string &sectionName);
    
private:
    std::vector<SymTab_entry> symTab;
    unsigned int locationCounter;
    unsigned short currentSection; // where we are now, for adding symbols
    std::string writePath; // output file location

    unsigned short findByName(const std::string &entryName) const;
    unsigned short getOrCreateSymbol(const std::string &symbolName);
};
