#pragma once

// The defense VM's g++ 9.4 defaults to gnu++14 but supports -std=c++17 fully;
// the Makefile passes the flag. Fail loudly if it ever goes missing, rather
// than with pages of template errors from std::variant below.
static_assert(__cplusplus >= 201703L,
              "This project requires C++17, build with -std=c++17");

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
using RegIndex = std::uint8_t;     // 0..15
using Displacement = std::int32_t; // SIGNED

// The two general purpose registers with a second name in the ISA.
// Stack grows downwards, and SP points ot the adress of the occupied adress at the top of the stack
constexpr RegIndex SP = 14;
constexpr RegIndex PC = 15;
// CSR indices: status is 0, handler is 1, cause is 2.
constexpr RegIndex CSR_STATUS = 0;

static constexpr SymIndex UND = 0;
constexpr SymIndex ABS = USHRT_MAX;

struct SymTab_entry {
    /*  Symbol table entry */    
    SymIndex index = 0;             // index in the symbol table
    unsigned int value = 0;         // offset within its section (0 for undefined or section)
    Type type = Type::NOTYP;              
    Bind bind = Bind::LOC;              
    SymIndex section_index = UND;   // index of the section it belongs to (can be NO_SECTION or ABS)
    std::string name;       
    bool is_defined = false;        // for forward declarations 
    bool is_extern = false;
 
    SymTab_entry(unsigned short index, const std::string &name) {
        this->index = index;
        this->name = name;
    }
};

// Literal pool index that encodes error value when pool index is expected
constexpr std::uint16_t NO_LITERAL_POOL_ENTRY = 4096;

struct LiteralPoolEntry {
    // true - use symbol field, false - use ltieral field
    bool is_symbol = false;
    std::uint32_t literal = 0;
    // Index of the symbol, necessary for relocation purposes
    // the literal pool slot will be a hole filled with 0 anyways
    SymIndex symbol = UND;

    bool operator==(const LiteralPoolEntry &other) const {
        return is_symbol == other.is_symbol
            && literal == other.literal
            && symbol == other.symbol;
    }
};

struct PoolFixup {
    std::uint32_t instruction_offset; // offset from the section start to the instruction that needs to be patched up
    std::uint16_t pool_index; // index of the literal pool entry inside of a section to which we need to point to
    // TODO make this POTENTIALLY also used for jump instructions where pc relative jump can be made to same-section later declared symbol
};

struct Reloc {
    std::uint32_t offset;   // offset from section start to the hole that needs to be patched up
    std::uint32_t addend;   // A - the offset of the symbol that needs to be relocated from the section start
    SymIndex symbol;        // S - the symbol being relocated against
};

static constexpr SymIndex NO_SECTION = UND;

struct Section {
    SymIndex symTab_index;
    std::vector<std::uint8_t> bytes;
    std::vector<Reloc> relocs;

    std::vector<LiteralPoolEntry> literal_pool;
    std::vector<PoolFixup> pool_fixups;
    explicit Section (SymIndex symTab_index) {
        this->symTab_index = symTab_index;
    }


    std::uint32_t getLocationCounter() const { return bytes.size(); }
};

// Shared with the emulator, which decodes the same field. Cast unsigned values with
// static_cast<Displacement> first: the field is sign extended when executed, so
// 0xFFFFFEFE is -258 and does fit.
constexpr Displacement DISPLACEMENT_MIN = -2048;
constexpr Displacement DISPLACEMENT_MAX = 2047;

constexpr bool fitsInDisplacement(Displacement disp) {
    return disp >= DISPLACEMENT_MIN && disp <= DISPLACEMENT_MAX;
}

// PC is incremented before execute, so a pc-relative field is measured from the
// instruction after this one, so we need to decrement the displacement by another word
// Both arguments are offsets within the same section, or both addresses
constexpr Displacement pcRelativeDisplacement(std::uint32_t target, std::uint32_t instruction) {
    return static_cast<Displacement>(target) - static_cast<Displacement>(instruction) - 4;
}

// Jump instructions accept only MemoryLiteral / MemorySymbol
enum class InstructionOperandKind {
    ImmediateLiteral,       // $<literal>          - the literal itself
    ImmediateSymbol,        // $<simbol>           - the symbol's value
    MemoryLiteral,          // <literal>           - memory at address <literal>
    MemorySymbol,           // <simbol>            - memory at address <simbol>
    RegisterDirect,         // %<reg>              - the register's value
    RegisterIndirect,       // [%<reg>]            - memory at address <reg>
    RegisterLiteralOffset,  // [%<reg> + <literal>] - memory at <reg> + <literal>
    RegisterSymbolOffset    // [%<reg> + <simbol>]  - memory at <reg> + <simbol>
};

// Which fields carry meaning depends on kind.
struct InstructionOperand {
    InstructionOperandKind kind;
    RegIndex reg = 0;
    std::uint32_t literal = 0;
    std::string symbol;
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
    
    // Directive handlers
    void handleWordSymbol(const std::string &symbol_name);
    void handleWordLiteral(const std::uint32_t literal);
    void openSection(const std::string &section_name);
    void declareGlobal(const std::string &symbol_name);
    void declareExtern(const std::string &symbol_name);
    void defineLabel(const std::string &label_name);
    void emitZeros(std::uint32_t number_of_zeros);

    // Handlers for instructions with register only operands. No displacement to resolve, no pool slot, no relocation.
    void instructionNoOperand(const std::string &mnemonic);
    void instructionOneRegister(const std::string &mnemonic, RegIndex reg);
    void instructionTwoRegisters(const std::string &mnemonic, RegIndex src, RegIndex dst);
    void instructionCsrRead(const std::string &mnemonic, RegIndex csr, RegIndex gpr);
    void instructionCsrWrite(const std::string &mnemonic, RegIndex gpr, RegIndex csr);
    
    // Handles for load/store instructions, who can take any operand type
    void instructionLoad(const std::string &mnemonic, const InstructionOperand &operand, RegIndex gpr);
    void instructionStore(const std::string &mnemonic, RegIndex gpr, const InstructionOperand &operand);

    // temporary, until the addressing modes are encoded
    void instructionUnimplemented(const std::string &mnemonic, const InstructionOperand &operand);

    void endAssembly(); // .end - writes the object file

    bool hadError() const { return error_count > 0; }

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

    // [%reg + symbol] needs the symbol's *final* value at assembly time, which only an
    // absolute symbol (declared via .equ) has. Errors and returns false otherwise.
    bool displacementFromSymbol(const std::string &symbol_name, Displacement &disp);

    // Checks if current sections literal_pool contains the operand contents (based on operand type)
    // If yes, returns its existing index, otherwise creates new entry in the literal pool and returns its index
    std::uint16_t getOrCreatePoolEntry(const InstructionOperand &operand);
    
    // emitting content into sections
    void emitByte(std::uint8_t byte);
    // little-endian, data only, instructions are not in little-endian order
    void emitWord(std::uint32_t word);

    void emitInstruction(std::uint8_t oc,
                         std::uint8_t mod,
                         RegIndex regA,
                         RegIndex regB,
                         RegIndex regC,
                         Displacement disp);

    void emitInstructionViaPool(std::uint8_t oc,
                                std::uint8_t mod,
                                RegIndex regA,
                                RegIndex regB,
                                RegIndex regC,
                                const InstructionOperand &operand);

    // records an R_32 relocation for the 4 bytes about to be emitted, must be called before the emit because it reads the location counter
    void relocateAgainst(SymIndex symbol_index);

    // reads location counter, also must be called before emitting instruction
    void makeBackpatchNote(std::uint16_t pool_index);

    // called at .end
    void flushLiteralPools();
    void resolveRelocs();

    // output
    void writeObjectFile();

    // error handling
    unsigned int error_count = 0;
    void error(const std::string &error_msg);
};
