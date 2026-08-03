# Builds the scratch bison+flex parser used while bringing up the assembler.
# Not part of the graded asembler build (see src/asembler/Makefile).
#
#   make            build the parser
#   make clean

CXX      := g++
CXXFLAGS := -std=c++17 -Wall
FLEX     := flex
BISON    := bison

# everything flex/bison/g++ generate lands here, not in misc/
BUILD    := build


.PHONY: all parser clean help

all: parser

help:
	@echo "Available targets:"
	@echo "  make help    - Show this help message"
	@echo "  make all     - Build the bison + flex parser"
	@echo "  make parser  - Same as 'make all'"
	@echo "  make clean   - Remove build artifacts"


# --- bison/flex parser (parser.y + lexer.l) ------------------------------
# bison -d also emits parser.tab.h; declared here so `make` reruns bison when
# either output is missing, without listing the .h as a second real target.
$(BUILD)/parser.tab.c: parser.y | $(BUILD)
	$(BISON) -d -o $@ $<

$(BUILD)/lex.yy.c: lexer.l | $(BUILD)
	$(FLEX) -o $@ $<

# -I$(BUILD): lex.yy.c does `#include "parser.tab.h"`, which only exists
# once bison has run -- that's why parser.tab.c is listed as a prerequisite.
# -I.: parser.y does `#include "../inc/assembler.hpp"`, resolved relative to
# misc/ rather than to build/ where the generated .c actually sits.
$(BUILD)/parser: $(BUILD)/parser.tab.c $(BUILD)/lex.yy.c
	$(CXX) $(CXXFLAGS) -I$(BUILD) -I. -o $@ $(BUILD)/parser.tab.c $(BUILD)/lex.yy.c

parser: $(BUILD)/parser

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
