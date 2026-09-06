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

bool Linker::link() {
    mergeSections();
    placeSections();
    if (hadError())
        return false;

    if (options.relocatable) {
        error("-relocatable is not implemented yet");
        return false;
    }
    return writeHex();
}
