#include "../../inc/linker.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

// Project_specs.md:174-176 lays the record out eight bytes to a row.
static constexpr std::uint32_t BYTES_PER_ROW = 8;
static constexpr std::uint32_t ROW_MASK = ~(BYTES_PER_ROW - 1);

// Upper case, as the example record in the spec is.
static std::string toHex(std::uint32_t value, int width) {
    std::ostringstream stream;
    stream << std::hex << std::uppercase << std::setfill('0') << std::setw(width) << value;
    return stream.str();
}

void Linker::error(const std::string &message) {
    std::cerr << "linker: " << message << "\n";
    error_count++;
}

MergedSection &Linker::mergedSection(const std::string &name) {
    for (MergedSection &section : merged_sections)
        if (section.name == name)
            return section;

    merged_sections.push_back(MergedSection{name});
    return merged_sections.back();
}

// Files in command line order, sections in definition order. That order is what
// Project_specs.md:152 fixes, and it decides both aggregation and default layout.
void Linker::mergeSections() {
    for (std::size_t file = 0; file < input_files.size(); file++) {
        for (const ObjSection &section : input_files[file].sections) {
            MergedSection &merged = mergedSection(section.name);
            merged.contributions.push_back({file, static_cast<std::uint32_t>(merged.bytes.size())});
            merged.bytes.insert(merged.bytes.end(), section.bytes.begin(), section.bytes.end());
        }
    }
}

void Linker::placeSections() {
    std::vector<bool> placed(merged_sections.size(), false);
    std::uint64_t cursor = 0;

    for (const Placement &placement : options.placements) {
        for (std::size_t i = 0; i < merged_sections.size(); i++) {
            // A -place naming a section no input file defines is ignored, as ld does
            if (merged_sections[i].name != placement.section)
                continue;
            merged_sections[i].address = placement.address;
            placed[i] = true;
            cursor = std::max(cursor, std::uint64_t(placement.address) + merged_sections[i].bytes.size());
        }
    }

    // Unplaced sections follow the end of the highest placed one, or start at
    // zero when nothing was placed. One cursor, no special case. Packed with
    // no padding between them, per Project_specs.md:152.
    for (std::size_t i = 0; i < merged_sections.size(); i++) {
        if (placed[i])
            continue;
        merged_sections[i].address = static_cast<std::uint32_t>(cursor);
        cursor += merged_sections[i].bytes.size();
    }

    for (const MergedSection &section : merged_sections)
        if (std::uint64_t(section.address) + section.bytes.size() > 0x100000000ULL)
            error("section " + section.name + " runs past the end of the address space");
}

bool Linker::writeHex() {
    // Sorted by address, which is what lets the emitter walk rows in order.
    std::map<std::uint32_t, std::uint8_t> memory;
    for (const MergedSection &section : merged_sections)
        for (std::size_t i = 0; i < section.bytes.size(); i++)
            memory[section.address + static_cast<std::uint32_t>(i)] = section.bytes[i];

    std::ofstream out(options.output_path);
    if (!out) {
        error("cannot open output file: " + options.output_path);
        return false;
    }

    for (auto it = memory.begin(); it != memory.end(); ) {
        std::uint32_t row = it->first & ROW_MASK;

        // Rows are emitted only when they hold something, and stop after the
        // last byte they hold, so a gap between sections costs no output.
        auto next = it;
        std::uint32_t last = it->first;
        while (next != memory.end() && (next->first & ROW_MASK) == row)
            last = next++->first;

        // Counted in 64 bits on purpose: a section ending at 0xFFFFFFFF is
        // legal, and a uint32_t counter would wrap past `last` and never stop.
        out << toHex(row, 8) << ":";
        for (std::uint64_t address = row; address <= last; address++) {
            auto byte = memory.find(static_cast<std::uint32_t>(address));
            out << " " << toHex(byte == memory.end() ? 0 : byte->second, 2);
        }
        out << "\n";
        it = next;
    }

    return out.good();
}

// An empty section occupies nothing and so cannot clash with anything.
void Linker::checkOverlaps() {
    for (std::size_t i = 0; i < merged_sections.size(); i++) {
        const MergedSection &first = merged_sections[i];
        if (first.bytes.empty())
            continue;
        std::uint64_t first_end = std::uint64_t(first.address) + first.bytes.size();

        for (std::size_t j = i + 1; j < merged_sections.size(); j++) {
            const MergedSection &second = merged_sections[j];
            if (second.bytes.empty())
                continue;
            std::uint64_t second_end = std::uint64_t(second.address) + second.bytes.size();

            if (first.address >= second_end || second.address >= first_end)
                continue;

            error("sections " + first.name + " and " + second.name + " overlap: "
                  + first.name + " at " + toHex(first.address, 8) + ".."
                  + toHex(static_cast<std::uint32_t>(first_end - 1), 8) + ", "
                  + second.name + " at " + toHex(second.address, 8) + ".."
                  + toHex(static_cast<std::uint32_t>(second_end - 1), 8));
        }
    }
}

MergedSection *Linker::findContribution(std::size_t file, const std::string &name,
                                       std::uint32_t &offset) {
    for (MergedSection &section : merged_sections) {
        if (section.name != name)
            continue;
        for (const Contribution &contribution : section.contributions)
            if (contribution.file == file) {
                offset = contribution.offset;
                return &section;
            }
    }
    return nullptr;
}

bool Linker::sectionBase(std::size_t file, const std::string &name, std::uint32_t &base) {
    std::uint32_t offset = 0;
    const MergedSection *section = findContribution(file, name, offset);
    if (!section)
        return false;
    base = section->address + offset;
    return true;
}

void Linker::collectGlobalSymbols() {
    for (std::size_t file = 0; file < input_files.size(); file++) {
        for (const ObjSymbol &symbol : input_files[file].symbols) {
            if (symbol.index == UND || symbol.section_index == UND)
                continue;                                   // null entry, or an import
            if (symbol.bind != Bind::GLOB || symbol.type == Type::SCTN)
                continue;                                   // stays inside its own file

            auto existing = global_symbols.find(symbol.name);
            if (existing != global_symbols.end()) {
                error("multiple definition of " + symbol.name + ", in "
                      + options.input_paths[existing->second.file] + " and "
                      + options.input_paths[file]);
                continue;
            }

            GlobalSymbol global;
            global.file = file;
            global.value = symbol.value;
            global.is_absolute = symbol.section_index == ABS;
            if (!global.is_absolute)
                global.section = input_files[file].symbols[symbol.section_index].name;
            global_symbols[symbol.name] = global;
        }
    }
}

bool Linker::symbolAddress(std::size_t file, SymIndex symbol, std::uint32_t &address) {
    const ObjSymbol &target = input_files[file].symbols[symbol];

    // An absolute symbol is its value, so no section base is added to it.
    if (target.section_index == ABS) {
        address = target.value;
        return true;
    }

    // Defined in this file, so the answer is local: a section symbol names its
    // own section and carries value 0, which makes this the section's address.
    if (target.section_index != UND) {
        std::uint32_t base = 0;
        if (!sectionBase(file, input_files[file].symbols[target.section_index].name, base))
            return false;
        address = base + target.value;
        return true;
    }

    // Imported (extern) - whichever file defines it decides where it is.
    auto found = global_symbols.find(target.name);
    if (found == global_symbols.end())
        return false;
    if (found->second.is_absolute) {
        address = found->second.value;
        return true;
    }

    std::uint32_t base = 0;
    if (!sectionBase(found->second.file, found->second.section, base))
        return false;
    address = base + found->second.value;
    return true;
}

// Data is little endian, and every R_32 hole is a data word --
// literal pool slots and .word initialisers both.
void Linker::patchWord(MergedSection &section, std::uint32_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; i++)
        section.bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

void Linker::resolveRelocations() {
    for (std::size_t file = 0; file < input_files.size(); file++) {
        for (const ObjSection &section : input_files[file].sections) {
            std::uint32_t contribution = 0;
            MergedSection *merged = findContribution(file, section.name, contribution);

            for (const Reloc &reloc : section.relocs) {
                std::uint32_t symbol_address = 0;
                if (!symbolAddress(file, reloc.symbol, symbol_address)) {
                    error("undefined symbol " + input_files[file].symbols[reloc.symbol].name
                          + ", referenced from " + options.input_paths[file]);
                    continue;
                }
                // R_32 is S + A, and the field was left zero by the assembler.
                patchWord(*merged, contribution + reloc.offset, symbol_address + reloc.addend);
            }
        }
    }
}

bool Linker::link() {
    mergeSections();
    collectGlobalSymbols();
    if (hadError())
        return false;

    if (options.relocatable) {
        // Sections carry no addresses here -- Project_specs.md:181 ignores
        // -place outright -- and relocations survive rather than being consumed.
        error("-relocatable is not implemented yet");
        return false;
    }

    // Placement is a -hex concern only, and so is the overlap it can create.
    placeSections();
    checkOverlaps();
    if (hadError())
        return false;

    resolveRelocations();
    if (hadError())
        return false;

    return writeHex();
}
