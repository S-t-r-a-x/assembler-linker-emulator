#include <cstdio>
#include <cstring>
#include <iostream>
#include "assembler.hpp"
#include "parser.tab.h"

extern FILE* yyin;

int main(int argc, char** argv) {
    const char* inputPath = nullptr;
    const char* outputPath = nullptr;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                std::cerr << "asembler: -o requires an argument\n";
                return 1;
            }
            outputPath = argv[++i];
        } else {
            inputPath = argv[i];
        }
    }

    // Both are required: the Assembler ctor takes std::string, and
    // constructing one from a null char* throws instead of erroring cleanly.
    if (!inputPath || !outputPath) {
        std::cerr << "usage: asembler -o <output> <input>\n";
        return 1;
    }

    yyin = fopen(inputPath, "r");
    if (!yyin) {
        std::cerr << "asembler: cannot open " << inputPath << "\n";
        return 1;
    }

    Assembler assembler(outputPath);
    yy::parser parser(assembler);
    int parser_result = parser.parse();
    bool assembler_finished_correctly = assembler.assemblerFinishedCorrectly();
    fclose(yyin);

    return parser_result || assembler_finished_correctly;
}
