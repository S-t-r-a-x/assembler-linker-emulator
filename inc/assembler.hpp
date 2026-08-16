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

using SymIndex = unsigned short;
// probably do something like 
// using Literal = std::uint32_t;
static constexpr SymIndex UND = 0;
constexpr SymIndex ABS = USHRT_MAX;

struct SymTab_entry {
    /*  Symbol table entry */    
    SymIndex index = 0;                     // index in the symbol table
    unsigned int value = 0;                 // offset within its section (0 for undefined or section)
    Type type = Type::NOTYP;              
    Bind bind = Bind::LOC;              
    SymIndex section_index = UND;    // index of the section it belongs to (can be NO_SECTION or ABS)
    std::string name;       
    bool is_defined = false;                           // for forward declarations 
    bool is_extern = false;
 
    SymTab_entry(unsigned short index, const std::string &name) {
        this->index = index;
        this->name = name;
    }
};

static constexpr SymIndex NO_SECTION = UND;

struct Section {
    SymIndex symTab_index;
    std::vector<std::uint8_t> bytes;

    explicit Section (SymIndex symTab_index) {
        this->symTab_index = symTab_index;
    }

    std::uint32_t getLocationCounter() const { return bytes.size(); }
};


class Assembler {
public:
    Assembler(std::string write_path) {
        this->write_path = write_path;

        // first entry in the symbol table - null entry, represents no symbol
        SymTab_entry &nullEntry = symbol_table[getOrCreateSymbol("")];
        nullEntry.value = 0;
        nullEntry.type = Type::NOTYP;
        nullEntry.bind = Bind::LOC;
        nullEntry.section_index = NO_SECTION;
        
        current_section = NO_SECTION;
    }

    void openSection(const std::string &section_name);
    
    void declareGlobal(const std::string &symbol_name);

    void declareExtern(const std::string &symbol_name);

    void defineLabel(const std::string &label_name);

    bool assemblerFinishedCorrectly() const { return error_count == 0; }

private:
    std::vector<SymTab_entry> symbol_table;
    std::vector<Section> section_table;
    SymIndex current_section; // index of the current section in the symbol table
    std::string write_path; // output file location

    // Symbol table
    SymIndex findSymbolByName(const std::string &entry_name) const;
    SymIndex getOrCreateSymbol(const std::string &symbol_name);

    // Section table
    Section *findSection(SymIndex symTab_index);
    
    // emitting content into sections
    void emitByte(std::uint8_t byte);
    void emitWord(std::uint32_t word); // little-endian, data only, instructions are not in little-endian order

    // error handling
    unsigned int error_count = 0;
    void error(const std::string &error_msg);
};
