#pragma once

// The object file format: the only contract between asembler and linker.
// These structs are the on-disk record shape, nothing more -- the assembler's
// richer Section/SymTab_entry (literal pools, fixups, is_extern) stay private
// to it, and the linker's merge-time structures stay private to the linker.
// Both meet here, so the format is written down exactly once.

#include <climits>
#include <cstdint>
#include <string>
#include <vector>

using SymIndex = unsigned short;

enum class Type {
    NOTYP,
    SCTN
};

enum class Bind {
    LOC,
    GLOB
};

// Section index sentinels. UND is also the index of the symbol table's null
// entry, so a symbol "in section 0" is exactly a symbol in no section.
constexpr SymIndex UND = 0;
constexpr SymIndex ABS = USHRT_MAX;

// Exactly one relocation type, R_32 (S + A), RELA style: the addend lives in
// the record and the 4-byte field in the section is left zero.
struct Reloc {
    std::uint32_t offset;   // offset from section start to the hole to patch
    std::uint32_t addend;   // A
    SymIndex symbol;        // S
};

struct ObjSymbol {
    SymIndex index = 0;
    std::uint32_t value = 0;        // offset within its section, or the value if ABS
    Type type = Type::NOTYP;
    Bind bind = Bind::LOC;
    SymIndex section_index = UND;   // UND, ABS, or the index of a section symbol
    std::string name;
};

struct ObjSection {
    std::string name;
    std::vector<std::uint8_t> bytes;
    std::vector<Reloc> relocs;
};

struct ObjectFile {
    // symbols[i].index == i: slot 0 holds the null entry, which is never
    // written to disk. Keeping it means a relocation's symbol field indexes
    // this vector directly, with no off-by-one on either side of the format.
    std::vector<ObjSymbol> symbols;
    std::vector<ObjSection> sections;
};

namespace objfile {

// False only if the output file could not be opened; the caller words the error.
bool write(const std::string &path, const ObjectFile &obj);

// Throws std::runtime_error, worded with the offending line, on anything
// malformed. Object files are machine-generated, so a deviation is a bug or a
// corrupt file -- never something worth recovering from.
ObjectFile read(const std::string &path);

} // namespace objfile
