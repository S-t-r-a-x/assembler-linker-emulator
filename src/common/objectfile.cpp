#include "../../inc/objectfile.hpp"
#include <charconv>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

// Cosmetic, but must stay fixed so re-emitting a parsed object file reproduces it exactly.
static constexpr std::size_t BYTES_PER_LINE = 16;

static std::string toHex(std::uint32_t value, int width) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(width) << value;
    return stream.str();
}

bool objfile::write(const std::string &path, const ObjectFile &obj) {
    std::ofstream out(path);
    if (!out)
        return false;

    // Slot 0 is the null entry and has no on-disk representation.
    const std::size_t symbol_count = obj.symbols.empty() ? 0 : obj.symbols.size() - 1;
    out << "SYMTAB " << symbol_count << "\n";
    for (const ObjSymbol &symbol : obj.symbols) {
        if (symbol.index == UND)
            continue;

        out << symbol.index << " "
            << toHex(symbol.value, 8) << " "
            << (symbol.type == Type::SCTN ? "SCTN" : "NOTYP") << " "
            << (symbol.bind == Bind::GLOB ? "GLOB" : "LOC") << " "
            << (symbol.section_index == ABS ? "ABS" : std::to_string(symbol.section_index)) << " "
            << symbol.name << "\n";
    }

    for (const ObjSection &section : obj.sections) {
        out << "\nSECTION " << section.name << " " << section.bytes.size() << "\n";

        for (std::size_t i = 0; i < section.bytes.size(); i++) {
            out << toHex(section.bytes[i], 2);
            bool end_of_line = (i + 1) % BYTES_PER_LINE == 0;
            bool last_byte = i + 1 == section.bytes.size();
            out << (end_of_line || last_byte ? "\n" : " ");
        }

        if (section.relocs.empty())
            continue;

        out << "\nRELOC " << section.name << " " << section.relocs.size() << "\n";

        for (const Reloc &reloc : section.relocs) {
            out << toHex(reloc.offset, 8) << " "
                << reloc.symbol << " "
                << toHex(reloc.addend, 8) << "\n";
        }
    }

    return out.good();
}

namespace {

class ObjectReader {
public:
    explicit ObjectReader(const std::string &path) : path(path), in(path) {
        if (!in)
            throw std::runtime_error("cannot open object file: " + path);
    }

    ObjectFile read() {
        ObjectFile obj;
        std::vector<std::string> tokens;

        if (!nextTokens(tokens) || tokens.size() != 2 || tokens[0] != "SYMTAB")
            fail("expected a SYMTAB header");
        readSymbolTable(obj, number(tokens[1], 10, "symbol count"));

        while (nextTokens(tokens)) {
            if (tokens.size() != 3)
                fail("expected a SECTION or RELOC header with a name and a count");
            ObjSection *section = findSection(obj, tokens[1]);

            if (tokens[0] == "SECTION") {
                if (section)
                    fail("section declared twice: " + tokens[1]);
                obj.sections.push_back(ObjSection{tokens[1], {}, {}});
                readSectionBytes(obj.sections.back(), number(tokens[2], 10, "section size"));
            } else if (tokens[0] == "RELOC") {
                if (!section)
                    fail("relocations for a section that was never declared: " + tokens[1]);
                if (!section->relocs.empty())
                    fail("a second RELOC block for section " + tokens[1]);
                readRelocs(*section, number(tokens[2], 10, "relocation count"), obj.symbols.size());
            } else {
                fail("expected SECTION or RELOC, found " + tokens[0]);
            }
        }
        return obj;
    }

private:
    const std::string path;
    std::ifstream in;
    std::size_t line_number = 0;

    [[noreturn]] void fail(const std::string &message) const {
        throw std::runtime_error(path + ":" + std::to_string(line_number) + ": " + message);
    }

    static ObjSection *findSection(ObjectFile &obj, const std::string &name) {
        for (ObjSection &section : obj.sections)
            if (section.name == name)
                return &section;
        return nullptr;
    }

    // Next line that holds anything, split on whitespace. False at end of file.
    bool nextTokens(std::vector<std::string> &tokens) {
        std::string line;
        while (std::getline(in, line)) {
            line_number++;
            tokens.clear();
            std::istringstream stream(line);
            for (std::string token; stream >> token; )
                tokens.push_back(token);
            if (!tokens.empty())
                return true;
        }
        return false;
    }

    // from_chars rather than stoul: it rejects trailing junk and a "0x" prefix
    // instead of quietly stopping at it, which is the point of being strict.
    std::uint32_t number(const std::string &token, int base, const char *what) {
        std::uint32_t value = 0;
        const char *last = token.data() + token.size();
        auto parsed = std::from_chars(token.data(), last, value, base);
        if (parsed.ec != std::errc() || parsed.ptr != last)
            fail(std::string(what) + " is not a number: " + token);
        return value;
    }

    SymIndex symIndex(const std::string &token, const char *what) {
        std::uint32_t value = number(token, 10, what);
        if (value >= ABS)
            fail(std::string(what) + " out of range: " + token);
        return static_cast<SymIndex>(value);
    }

    void readSymbolTable(ObjectFile &obj, std::uint32_t count) {
        obj.symbols.push_back(ObjSymbol{});   // slot 0, the null entry the format omits

        std::vector<std::string> tokens;
        for (std::uint32_t i = 1; i <= count; i++) {
            if (!nextTokens(tokens))
                fail("file ends inside the symbol table");
            if (tokens.size() != 6)
                fail("a symbol table entry has six fields, found " + std::to_string(tokens.size()));

            ObjSymbol symbol;
            // symbols[i].index == i is an ObjectFile invariant: it is what lets
            // a relocation's symbol field index the vector with no translation.
            symbol.index = symIndex(tokens[0], "symbol index");
            if (symbol.index != i)
                fail("symbol indices must run 1..n in order, expected " + std::to_string(i));
            symbol.value = number(tokens[1], 16, "symbol value");

            if (tokens[2] == "SCTN")       symbol.type = Type::SCTN;
            else if (tokens[2] == "NOTYP") symbol.type = Type::NOTYP;
            else fail("unknown symbol type: " + tokens[2]);

            if (tokens[3] == "GLOB")     symbol.bind = Bind::GLOB;
            else if (tokens[3] == "LOC") symbol.bind = Bind::LOC;
            else fail("unknown symbol binding: " + tokens[3]);

            symbol.section_index = tokens[4] == "ABS" ? ABS : symIndex(tokens[4], "section index");
            symbol.name = tokens[5];
            obj.symbols.push_back(symbol);
        }

        // Section references can point forward, so they wait for the whole table.
        for (const ObjSymbol &symbol : obj.symbols)
            if (symbol.section_index != UND && symbol.section_index != ABS
                    && (symbol.section_index >= obj.symbols.size()
                        || obj.symbols[symbol.section_index].type != Type::SCTN))
                fail("symbol " + symbol.name + " names symbol "
                     + std::to_string(symbol.section_index) + ", which is not a section");
    }

    void readSectionBytes(ObjSection &section, std::uint32_t size) {
        section.bytes.reserve(size);

        std::vector<std::string> tokens;
        while (section.bytes.size() < size) {
            // A block header here means the byte count was overstated. Caught by
            // name because a byte token is always two hex digits, never a keyword.
            if (!nextTokens(tokens) || tokens[0] == "SECTION" || tokens[0] == "RELOC")
                fail("section " + section.name + " ends after "
                     + std::to_string(section.bytes.size()) + " of "
                     + std::to_string(size) + " bytes");

            for (const std::string &token : tokens) {
                if (section.bytes.size() == size)
                    fail("section " + section.name + " holds more bytes than its declared size");
                std::uint32_t byte = number(token, 16, "section byte");
                if (byte > 0xFF)
                    fail("section byte does not fit in a byte: " + token);
                section.bytes.push_back(static_cast<std::uint8_t>(byte));
            }
        }
    }

    void readRelocs(ObjSection &section, std::uint32_t count, std::size_t symbol_count) {
        std::vector<std::string> tokens;
        for (std::uint32_t i = 0; i < count; i++) {
            if (!nextTokens(tokens))
                fail("file ends inside the relocations for section " + section.name);
            if (tokens.size() != 3)
                fail("a relocation entry has three fields, found " + std::to_string(tokens.size()));

            Reloc reloc;
            reloc.offset = number(tokens[0], 16, "relocation offset");
            reloc.symbol = symIndex(tokens[1], "relocation symbol");
            reloc.addend = number(tokens[2], 16, "relocation addend");

            // The patched field is four bytes wide and has to land inside the section.
            if (static_cast<std::size_t>(reloc.offset) + 4 > section.bytes.size())
                fail("relocation at offset " + tokens[0] + " reaches past the end of section "
                     + section.name);
            if (reloc.symbol == UND || reloc.symbol >= symbol_count)
                fail("relocation names symbol " + tokens[1] + ", which is not in the symbol table");

            section.relocs.push_back(reloc);
        }
    }
};

} // namespace

ObjectFile objfile::read(const std::string &path) {
    return ObjectReader(path).read();
}
