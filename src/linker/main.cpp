#include "../../inc/linker.hpp"
#include "../../inc/objectfile.hpp"
#include <charconv>
#include <iostream>

static bool parseAddress(const std::string &text, std::uint32_t &address) {
    bool is_hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    const char *first = text.data() + (is_hex ? 2 : 0);
    const char *last = text.data() + text.size();

    auto result = std::from_chars(first, last, address, is_hex ? 16 : 10);
    return result.ec == std::errc() && result.ptr == last;
}

// -place=<section>@<address>
static bool parsePlacement(const std::string &argument, LinkerOptions &options) {
    std::size_t at = argument.find('@');
    if (at == std::string::npos || at == 0 || at + 1 == argument.size()) {
        std::cerr << "linker: -place needs <section>@<address>, got: " << argument << "\n";
        return false;
    }

    Placement placement;
    placement.section = argument.substr(0, at);
    if (!parseAddress(argument.substr(at + 1), placement.address)) {
        std::cerr << "linker: not an address: " << argument.substr(at + 1) << "\n";
        return false;
    }

    for (const Placement &existing : options.placements) {
        if (existing.section == placement.section) {
            std::cerr << "linker: section placed twice: " << placement.section << "\n";
            return false;
        }
    }

    options.placements.push_back(placement);
    return true;
}

// False on a bad command line, having already written the reason to stderr.
static bool parseCommandLine(int argc, char **argv, LinkerOptions &options) {
    for (int i = 1; i < argc; i++) {
        std::string argument = argv[i];

        if (argument == "-hex") {
            options.hex = true;
        } else if (argument == "-relocatable") {
            options.relocatable = true;
        } else if (argument == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "linker: -o requires an argument\n";
                return false;
            }
            options.output_path = argv[++i];
        } else if (argument.rfind("-place=", 0) == 0) {
            if (!parsePlacement(argument.substr(7), options))
                return false;
        } else if (!argument.empty() && argument[0] == '-') {
            std::cerr << "linker: unknown option: " << argument << "\n";
            return false;
        } else {
            options.input_paths.push_back(argument);
        }
    }

    if (options.hex == options.relocatable) {
        std::cerr << "linker: exactly one of -hex and -relocatable is required\n";
        return false;
    }
    if (options.input_paths.empty()) {
        std::cerr << "usage: linker (-hex | -relocatable) [-o <output>]"
                     " [-place=<section>@<address>]... <input>...\n";
        return false;
    }
    return true;
}

int main(int argc, char **argv) {
    LinkerOptions options;
    if (!parseCommandLine(argc, argv, options))
        return 1;

    // Input order is significant: it fixes the order sections are merged in.
    std::vector<ObjectFile> inputs;
    for (const std::string &path : options.input_paths) {
        try {
            inputs.push_back(objfile::read(path));
        } catch (const std::exception &error) {
            std::cerr << "linker: " << error.what() << "\n";
            return 1;
        }
    }

    Linker linker(options, inputs);
    return linker.link() ? 0 : 1;
}
