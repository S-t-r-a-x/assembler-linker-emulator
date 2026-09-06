#pragma once

// The defense VM's g++ 9.4 defaults to gnu++14 but supports -std=c++17 fully;
// the Makefile passes the flag. Fail loudly if it ever goes missing, rather
// than with pages of template errors from std::variant below.
static_assert(__cplusplus >= 201703L,
              "This project requires C++17, build with -std=c++17");

#include <string>
#include <vector>
#include <variant>
#include <cstdint>

#include "objectfile.hpp"

// Pending means a term is not defined yet, so retry at .end. Invalid is already reported
enum class ExprResult {
    Resolved,
    Pending,
    Invalid
};

using RegIndex = std::uint8_t;     // 0..15
using Displacement = std::int32_t; // SIGNED

// The two general purpose registers with a second name in the ISA.
// Stack grows downwards, and SP points ot the adress of the occupied adress at the top of the stack
constexpr RegIndex SP = 14;
constexpr RegIndex PC = 15;
// CSR indices: status is 0, handler is 1, cause is 2.
constexpr RegIndex CSR_STATUS = 0;

struct SymTab_entry {
    /*  Symbol table entry */    
    SymIndex index = 0;             // index in the symbol table
    unsigned int value = 0;         // offset within its section (0 for undefined or section)
    Type type = Type::NOTYP;              
    Bind bind = Bind::LOC;              
    SymIndex section_index = UND;   // index of the section it belongs to (can be NO_SECTION or ABS)
    std::string name;       
    bool is_defined = false;        // a definition was seen in this file, even if its value is not resolved yet
    bool is_extern = false;
 
    SymTab_entry(unsigned short index, const std::string &name) {
        this->index = index;
        this->name = name;
    }
};

struct SymbolTerm {
    std::string symbol_name;
    short sign; // +1 for '+', -1 for '-'
};

struct Expression {
    std::int32_t constant = 0;
    std::vector<SymbolTerm> terms;

    Expression() = default;
    Expression(std::int32_t constant) : constant(constant) {}
    Expression(SymbolTerm st) { terms.push_back(st); }
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

// Essential part of the literal pool implementation
struct PoolFixup {
    std::uint32_t instruction_offset; // offset from the section start to the instruction that needs to be patched up
    std::uint16_t pool_index; // index of the literal pool entry inside of a section to which we need to point to
};

// Used for jump instruction, to be able to implement PC relative jumps to same section symbols
struct JumpFixup {
    std::uint32_t instruction_offset;
    SymIndex symbol;
};

static constexpr SymIndex NO_SECTION = UND;

struct Section {
    SymIndex symTab_index;
    std::vector<std::uint8_t> bytes;
    std::vector<Reloc> relocs;

    std::vector<LiteralPoolEntry> literal_pool;
    std::vector<PoolFixup> pool_fixups;
    std::vector<JumpFixup> jump_fixups;
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

// The jump family has a direct and a memory indirect form that differ by one MOD bit:
// bit 0 for call (OC 2), bit 3 for jmp and the branches (OC 3).
constexpr std::uint8_t indirectBit(std::uint8_t oc) {
    return oc == 0x2 ? 0x1 : 0x8;
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

// necessary for .equ directive
struct PendingEqu {
    SymIndex symbol; // Index of the symbol that we need to reevaluate
    Expression expression; // expression that needs to be evaluated
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
    void handleAscii(const std::string &str);
    void defineEquSymbol(const std::string &symbol_name, const Expression &expression);

    // Handlers for instructions with register only operands. No displacement to resolve, no pool slot, no relocation.
    void instructionNoOperand(const std::string &mnemonic);
    void instructionOneRegister(const std::string &mnemonic, RegIndex reg);
    void instructionTwoRegisters(const std::string &mnemonic, RegIndex src, RegIndex dst);
    void instructionCsrRead(const std::string &mnemonic, RegIndex csr, RegIndex gpr);
    void instructionCsrWrite(const std::string &mnemonic, RegIndex gpr, RegIndex csr);
    void instructionOneOperand(const std::string &mnemonic, const InstructionOperand &operand);
    void instructionConditionalBranch(const std::string &mnemonic,
                                      RegIndex reg1,
                                      RegIndex reg2,
                                      const InstructionOperand &operand);
    
    // Handles for load/store instructions, who can take any operand type
    void instructionLoad(const std::string &mnemonic, const InstructionOperand &operand, RegIndex gpr);
    void instructionStore(const std::string &mnemonic, RegIndex gpr, const InstructionOperand &operand);

    // temporary, until the addressing modes are encoded
    void instructionUnimplemented(const std::string &mnemonic, const InstructionOperand &operand);

    void endAssembly(); // .end - writes the object file

    bool hadError() const { return error_count > 0; }
    // Public because the parser reports syntax errors through the same counter.
    void error(const std::string &error_msg);

private:
    std::vector<SymTab_entry> symbol_table;
    std::vector<Section> section_table;
    std::vector<PendingEqu> pending_equs_table; 
    SymIndex current_section; // index of the current section in the symbol table
    std::string write_path; // output file location
    unsigned int error_count = 0; // error handling

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

    // jmp, call and the conditional branch instructions share one operand switch
    // cond is 0 for the unconditional ones (jmp and call) - cond determines the mod
    void emitJumpInstruction(std::uint8_t oc,
                             std::uint8_t cond,
                             RegIndex regB,
                             RegIndex regC,
                             const InstructionOperand &operand);

    // records an R_32 relocation for the 4 bytes about to be emitted, must be called before the emit because it reads the location counter
    void relocateAgainst(SymIndex symbol_index);

    // Must be called before emitting each jump instruction that has a symbol operand
    void makeJumpFixup(SymIndex symbol);
    // reads location counter, also must be called before emitting instruction
    void makePoolFixup(std::uint16_t pool_index);

    // used for .equ defined expressions, true signifies resolve, false signifies no change
    ExprResult evaluateEquSymbol(SymIndex sym, const Expression &expression);
    void resolveEquSymbols();

    // called at .end
    void resolveJumpFixups();
    void flushLiteralPools();
    void resolveRelocs();

    // output
    void writeObjectFile();
};
