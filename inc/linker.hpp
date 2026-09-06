#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "objectfile.hpp"

// -place=<section>@<address>
struct Placement {
    std::string section;
    std::uint32_t address;
};

// What the command line asked for: filled in by main, consumed by the linker.
// Exactly one of hex and relocatable is set -- Project_specs.md:179 makes that
// the condition for producing any output at all, so it is checked at parse time.
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

class Linker {
public:
    Linker(const LinkerOptions &options, const std::vector<ObjectFile> &input_files) : options(options), input_files(input_files) {}

    // False having already reported to stderr; no output file is written then.
    bool link();

private:
    LinkerOptions options;
    std::vector<ObjectFile> input_files;
    std::vector<MergedSection> merged_sections;
    unsigned error_count = 0;

    void error(const std::string &message);
    bool hadError() const { return error_count > 0; }

    // Finds the merged section of this name, creating an empty one on first sight.
    MergedSection &mergedSection(const std::string &name);

    void mergeSections();
    void placeSections();
    bool writeHex();
};
