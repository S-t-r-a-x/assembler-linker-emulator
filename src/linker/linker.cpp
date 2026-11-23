#include "../../inc/linker.hpp"
#include "../../inc/hexfile.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

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

// Files in command line order, sections in definition order. That fixed
// order decides both aggregation and default layout.
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

    // Unplaced sections pack tightly right after the last placed one (or start
    // at zero if nothing was placed): one cursor, no special-casing.
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
    // hexfile owns the record layout; the linker just says which byte
    // ended up at which address.
    hexfile::Image image;
    for (const MergedSection &section : merged_sections)
        for (std::size_t i = 0; i < section.bytes.size(); i++)
            image[section.address + static_cast<std::uint32_t>(i)] = section.bytes[i];

    if (!hexfile::write(options.output_path, image)) {
        error("cannot open output file: " + options.output_path);
        return false;
    }
    return true;
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
            if (symbol.section_index != ABS)
                global.section = input_files[file].symbols[symbol.section_index].name;
            global_symbols[symbol.name] = global;
        }
    }
}

const ObjSection *Linker::sourceSection(std::size_t file, const std::string &name) const {
    for (const ObjSection &section : input_files[file].sections)
        if (section.name == name)
            return &section;
    return nullptr;
}

bool Linker::definitionOf(std::size_t file, SymIndex symbol, GlobalSymbol &definition) {
    const ObjSymbol &target = input_files[file].symbols[symbol];

    // defined right here. section symbols land here too: one names its own
    // section and carries value 0, so it comes out as that section plus nothing
    if (target.section_index != UND) {
        definition.file = file;
        definition.value = target.value;
        definition.section = target.section_index == ABS
                           ? "" : input_files[file].symbols[target.section_index].name;
        return true;
    }

    // an import, so whichever file defines it decides
    auto found = global_symbols.find(target.name);
    if (found == global_symbols.end())
        return false;
    definition = found->second;
    return true;
}

bool Linker::symbolAddress(std::size_t file, SymIndex symbol, std::uint32_t &address) {
    GlobalSymbol definition;
    if (!definitionOf(file, symbol, definition))
        return false;

    // an absolute symbol is its value, no section base is added to it
    if (definition.section.empty()) {
        address = definition.value;
        return true;
    }

    std::uint32_t base = 0;
    if (!sectionBase(definition.file, definition.section, base))
        return false;
    address = base + definition.value;
    return true;
}

// Data is little endian, and every R_32 hole is a data word:
// literal pool slots and .word initialisers both.
void Linker::patchWord(std::vector<std::uint8_t> &bytes, std::uint32_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; i++)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
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
                patchWord(merged->bytes, contribution + reloc.offset, symbol_address + reloc.addend);
            }
        }
    }
}

// -relocatable outputs another object file, so relocations get rewritten rather
// than resolved. Locals are dropped entirely: the assembler's relocation rule
// guarantees no record ever points at one.
void Linker::writeRelocatable() {
    ObjectFile out;
    std::map<std::string, SymIndex> index_of;   // output symbol index, by name
    out.symbols.push_back(ObjSymbol{});         // null entry, like the assembler's

    // one section symbol per merged section, self referencing as the assembler writes them
    for (const MergedSection &merged : merged_sections) {
        ObjSymbol symbol;
        symbol.index = static_cast<SymIndex>(out.symbols.size());
        symbol.type = Type::SCTN;
        symbol.section_index = symbol.index;
        symbol.name = merged.name;
        index_of[merged.name] = symbol.index;
        out.symbols.push_back(symbol);
    }

    // exports survive so a later link can still find them. an absolute one stays
    // absolute, its value does not move when sections do
    for (const auto &[name, definition] : global_symbols) {
        ObjSymbol symbol;
        symbol.index = static_cast<SymIndex>(out.symbols.size());
        symbol.bind = Bind::GLOB;
        symbol.name = name;
        if (definition.section.empty()) {
            symbol.section_index = ABS;
            symbol.value = definition.value;
        } else {
            std::uint32_t base = 0;
            findContribution(definition.file, definition.section, base);
            symbol.section_index = index_of[definition.section];
            symbol.value = base + definition.value;
        }
        index_of[name] = symbol.index;
        out.symbols.push_back(symbol);
    }

    // imports nobody defined stay unresolved, which is allowed here but not for -hex
    for (const ObjectFile &file : input_files) {
        for (const ObjSymbol &symbol : file.symbols) {
            if (symbol.index == UND || symbol.section_index != UND || index_of.count(symbol.name))
                continue;
            ObjSymbol import;
            import.index = static_cast<SymIndex>(out.symbols.size());
            import.bind = Bind::GLOB;
            import.name = symbol.name;
            index_of[symbol.name] = import.index;
            out.symbols.push_back(import);
        }
    }

    for (const MergedSection &merged : merged_sections) {
        ObjSection section;
        section.name = merged.name;
        section.bytes = merged.bytes;

        for (const Contribution &contribution : merged.contributions) {
            for (const Reloc &reloc : sourceSection(contribution.file, merged.name)->relocs) {
                // wherever the hole was in its own file, it is this far in now
                std::uint32_t offset = contribution.offset + reloc.offset;

                GlobalSymbol definition;
                if (!definitionOf(contribution.file, reloc.symbol, definition)) {
                    // still an import, so only the hole moved
                    const std::string &name = input_files[contribution.file].symbols[reloc.symbol].name;
                    section.relocs.push_back({offset, reloc.addend, index_of[name]});
                    continue;
                }

                if (definition.section.empty()) {
                    // absolute, so S + A is final already and no record is needed
                    patchWord(section.bytes, offset, definition.value + reloc.addend);
                    continue;
                }

                // point it at the merged section symbol, folding the symbol's
                // own offset into the addend
                std::uint32_t base = 0;
                findContribution(definition.file, definition.section, base);
                section.relocs.push_back({offset, reloc.addend + base + definition.value,
                                          index_of[definition.section]});
            }
        }
        out.sections.push_back(std::move(section));
    }

    if (!objfile::write(options.output_path, out))
        error("cannot open output file: " + options.output_path);
}

bool Linker::link() {
    mergeSections();
    collectGlobalSymbols();
    if (hadError())
        return false;

    // sections get no addresses here: -relocatable ignores -place outright,
    // so there is nothing to place and nothing that can overlap
    if (options.relocatable) {
        writeRelocatable();
        return !hadError();
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
