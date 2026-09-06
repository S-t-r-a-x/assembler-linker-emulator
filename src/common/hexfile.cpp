#include "../../inc/hexfile.hpp"
#include <charconv>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

// Project_specs.md:174-176 lays the record out eight bytes to a row.
static constexpr std::uint32_t BYTES_PER_ROW = 8;
static constexpr std::uint32_t ROW_MASK = ~(BYTES_PER_ROW - 1);

// Upper case, as the example record in the spec is.
static std::string toHex(std::uint32_t value, int width) {
    std::ostringstream stream;
    stream << std::hex << std::uppercase << std::setfill('0') << std::setw(width) << value;
    return stream.str();
}

bool hexfile::write(const std::string &path, const Image &image) {
    std::ofstream out(path);
    if (!out)
        return false;

    for (auto it = image.begin(); it != image.end(); ) {
        std::uint32_t row = it->first & ROW_MASK;

        // Rows are emitted only when they hold something, and stop after the
        // last byte they hold, so a gap between sections costs no output.
        auto next = it;
        std::uint32_t last = it->first;
        while (next != image.end() && (next->first & ROW_MASK) == row)
            last = next++->first;

        // Counted in 64 bits on purpose: a section ending at 0xFFFFFFFF is
        // legal, and a uint32_t counter would wrap past `last` and never stop.
        out << toHex(row, 8) << ":";
        for (std::uint64_t address = row; address <= last; address++) {
            auto byte = image.find(static_cast<std::uint32_t>(address));
            out << " " << toHex(byte == image.end() ? 0 : byte->second, 2);
        }
        out << "\n";
        it = next;
    }

    return out.good();
}

namespace {

[[noreturn]] void fail(const std::string &path, std::size_t line_number, const std::string &message) {
    throw std::runtime_error(path + ":" + std::to_string(line_number) + ": " + message);
}

// from_chars rather than stoul: it rejects trailing junk and a "0x" prefix
// instead of quietly stopping at it, which is the point of being strict.
std::uint32_t hexNumber(const std::string &token, const char *what,
                        const std::string &path, std::size_t line_number) {
    std::uint32_t value = 0;
    const char *last = token.data() + token.size();
    auto parsed = std::from_chars(token.data(), last, value, 16);
    if (parsed.ec != std::errc() || parsed.ptr != last)
        fail(path, line_number, std::string(what) + " is not a hex number: " + token);
    return value;
}

} // namespace

hexfile::Image hexfile::read(const std::string &path) {
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("cannot open memory image: " + path);

    Image image;
    std::string line;
    for (std::size_t line_number = 1; std::getline(in, line); line_number++) {
        // The colon is the only punctuation in the format, so it is what splits
        // a row into its address and its bytes.
        std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            if (line.find_first_not_of(" \t\r") == std::string::npos)
                continue;
            fail(path, line_number, "expected a row of the form <address>: <byte>...");
        }

        std::istringstream head(line.substr(0, colon));
        std::string address_token, extra;
        if (!(head >> address_token) || (head >> extra))
            fail(path, line_number, "expected exactly one address before the colon");
        std::uint32_t address = hexNumber(address_token, "row address", path, line_number);

        std::istringstream bytes(line.substr(colon + 1));
        for (std::string token; bytes >> token; ) {
            std::uint32_t byte = hexNumber(token, "image byte", path, line_number);
            if (byte > 0xFF)
                fail(path, line_number, "image byte does not fit in a byte: " + token);
            image[address++] = static_cast<std::uint8_t>(byte);
        }
    }

    return image;
}
