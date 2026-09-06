#include "../../inc/emulator.hpp"
#include "../../inc/hexfile.hpp"
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: emulator <memory image>\n";
        return 1;
    }

    try {
        // Read before the Emulator exists, so a malformed image is reported
        // without the terminal ever having been put into raw mode.
        Emulator emulator(hexfile::read(argv[1]));
        emulator.run();
    } catch (const std::exception &error) {
        std::cerr << "emulator: " << error.what() << "\n";
        return 1;
    }

    return 0;
}
