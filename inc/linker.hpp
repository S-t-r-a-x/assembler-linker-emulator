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

class Linker {
public:
    Linker(const LinkerOptions &options, const std::vector<ObjectFile> &input_files) : options(options), input_files(input_files) {}

private:
    LinkerOptions options;
    std::vector<ObjectFile> input_files;
};