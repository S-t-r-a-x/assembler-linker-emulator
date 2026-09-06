#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "objectfile.hpp"

// -place=<section>@<address>
struct Placement {
    std::string section;
    std::uint32_t address;
};

// What the command line asked for, set by main
// Exactly one of hex and relocatable has to be set, checked at parse time
struct LinkerOptions {
    bool hex = false;
    bool relocatable = false;
    std::string output_path = "a.out";      // ld's default when -o is omitted
    std::vector<Placement> placements;
    std::vector<std::string> input_paths;
};

// Where one input file's section landed inside the merged section of that name.
// Every address the linker computes is merged.address + offset + a local offset,
// so this table is the only bookkeeping the later passes need.
struct Contribution {
    std::size_t file;       // indexes input_files, and options.input_paths alongside it
    std::uint32_t offset;   // start of this file's chunk within the merged section
};

struct MergedSection {
    std::string name;
    std::uint32_t address = 0;          // assigned by the placement pass
    std::vector<std::uint8_t> bytes;    // every contribution, concatenated
    std::vector<Contribution> contributions;
};

// Where a symbol is actually defined. Doubles as the answer for a relocation
// target, since resolving one is exactly the question "who defines this".
// Locals never appear here: nothing outside their own file can name them.
struct GlobalSymbol {
    std::size_t file = 0;       // the file holding the definition
    std::string section;        // section it lives in, empty means absolute
    std::uint32_t value = 0;    // offset in that section, or the value itself when absolute
};

class Linker {
public:
    Linker(const LinkerOptions &options, const std::vector<ObjectFile> &input_files) : options(options), input_files(input_files) {}

    // False having already reported to stderr; no output file is written then.
    bool link();

private:
    LinkerOptions options;
    std::vector<ObjectFile> input_files;
    std::vector<MergedSection> merged_sections;
    std::map<std::string, GlobalSymbol> global_symbols;
    unsigned error_count = 0;

    void error(const std::string &message);
    bool hadError() const { return error_count > 0; }

    // Finds the merged section of this name, creating an empty one on first sight.
    MergedSection &mergedSection(const std::string &name);

    // The merged section this file's section became part of, and where in it.
    MergedSection *findContribution(std::size_t file, const std::string &name, std::uint32_t &offset);
    // Address the file's own contribution to that section starts at.
    bool sectionBase(std::size_t file, const std::string &name, std::uint32_t &base);
    // That file's own section of this name, as it was read in.
    const ObjSection *sourceSection(std::size_t file, const std::string &name) const;
    // Who defines a relocation's target. False when nothing does, which is an
    // error for -hex and just an import that survives for -relocatable.
    bool definitionOf(std::size_t file, SymIndex symbol, GlobalSymbol &definition);
    // Final address of a relocation's target symbol. False if nothing defines it.
    bool symbolAddress(std::size_t file, SymIndex symbol, std::uint32_t &address);
    static void patchWord(std::vector<std::uint8_t> &bytes, std::uint32_t offset, std::uint32_t value);

    void mergeSections();
    void placeSections();
    void checkOverlaps();
    void collectGlobalSymbols();
    void resolveRelocations();
    bool writeHex();
    void writeRelocatable();
};
