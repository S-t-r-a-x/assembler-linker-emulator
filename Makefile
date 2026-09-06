# Root build for the whole toolchain: asembler, linker (later: emulator).
#
#   make            build bin/asembler and bin/linker
#   make parser     generate + compile just the bison parser (fast error check)
#   make lexer      generate + compile just the flex lexer (fast error check)
#   make clean
#   make help

CXX      := g++
CXXFLAGS := -std=c++17 -Wall -MMD -MP
BISON    := bison
FLEX     := flex

BIN_DIR   := bin
BUILD_DIR := build
INC_DIR   := inc

# One object directory per tool, mirroring src/. Keeps each tool's objects
# apart so two tools can both have a main.cpp, and lets every compile rule
# have its own target pattern instead of sharing one.
ASEMBLER_BUILD := $(BUILD_DIR)/asembler
LINKER_BUILD   := $(BUILD_DIR)/linker
COMMON_BUILD   := $(BUILD_DIR)/common

.PHONY: all parser lexer clean help

all: $(BIN_DIR)/asembler $(BIN_DIR)/linker

help:
	@echo "Available targets:"
	@echo "  make            - build bin/asembler and bin/linker (default)"
	@echo "  make parser     - generate + compile the bison parser only, no link"
	@echo "                    (catches grammar conflicts and C++ errors in actions fast)"
	@echo "  make lexer      - generate + compile the flex lexer only, no link"
	@echo "  make clean      - remove build/ and bin/"
	@echo "  make help       - show this message"

# --- shared -----------------------------------------------------------
# src/common holds what more than one tool needs -- currently the object file
# format, which asembler writes and linker reads.
$(COMMON_BUILD)/%.o: src/common/%.cpp | $(COMMON_BUILD)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -c -o $@ $<

# --- asembler ---------------------------------------------------------
# Grammar sources live in misc/ (see Project_specs.md: flex/bison inputs
# belong there, their generated output must never be submitted). Their
# generated output is asembler-only, so it lands in that tool's build dir.
$(ASEMBLER_BUILD)/parser.tab.c: misc/parser.y | $(ASEMBLER_BUILD)
	$(BISON) -d -o $@ $<

# `bison -d` emits parser.tab.h from the SAME run that produces parser.tab.c.
# A grouped target (`a b &: dep`) states that directly, but it needs GNU Make
# 4.3+ and the defense VM (Ubuntu 20.04) ships make 4.2.1, where `&` is just
# another target name -- the rule silently degrades to independent targets and
# can invoke bison twice under `make -j`. This empty-recipe rule (the `;`) is
# the portable equivalent: the .h is refreshed as a side effect of building
# the .c, and there is no second recipe that could race with it.
# Caveat: deleting only parser.tab.h while the .c is current won't regenerate
# it -- `make clean` is the fix, and build/ is wiped wholesale anyway.
$(ASEMBLER_BUILD)/parser.tab.h: $(ASEMBLER_BUILD)/parser.tab.c ;

$(ASEMBLER_BUILD)/lex.yy.c: misc/lexer.l | $(ASEMBLER_BUILD)
	$(FLEX) -o $@ $<

# Compiled separately (rather than fed straight to the final link command)
# so `make parser`/`make lexer` can surface C++ errors in grammar actions
# without waiting on the rest of the build, and so unrelated files don't
# get recompiled on every change.
$(ASEMBLER_BUILD)/parser.tab.o: $(ASEMBLER_BUILD)/parser.tab.c | $(ASEMBLER_BUILD)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -I$(ASEMBLER_BUILD) -c -o $@ $<

# lex.yy.c #includes parser.tab.h (for the token codes), so it must exist
# before this compiles -- not just lex.yy.c itself.
$(ASEMBLER_BUILD)/lex.yy.o: $(ASEMBLER_BUILD)/lex.yy.c $(ASEMBLER_BUILD)/parser.tab.h | $(ASEMBLER_BUILD)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -I$(ASEMBLER_BUILD) -c -o $@ $<

parser: $(ASEMBLER_BUILD)/parser.tab.o

lexer: $(ASEMBLER_BUILD)/lex.yy.o

# -I$(ASEMBLER_BUILD) is what lets main.cpp say #include "parser.tab.h".
$(ASEMBLER_BUILD)/%.o: src/asembler/%.cpp | $(ASEMBLER_BUILD)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -I$(ASEMBLER_BUILD) -c -o $@ $<

# main.cpp #includes parser.tab.h directly.
$(ASEMBLER_BUILD)/main.o: $(ASEMBLER_BUILD)/parser.tab.h

ASEMBLER_OBJS := $(ASEMBLER_BUILD)/main.o $(ASEMBLER_BUILD)/assembler.o \
                 $(ASEMBLER_BUILD)/parser.tab.o $(ASEMBLER_BUILD)/lex.yy.o \
                 $(COMMON_BUILD)/objectfile.o

$(BIN_DIR)/asembler: $(ASEMBLER_OBJS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(ASEMBLER_OBJS)

# --- linker -----------------------------------------------------------
$(LINKER_BUILD)/%.o: src/linker/%.cpp | $(LINKER_BUILD)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -c -o $@ $<

LINKER_OBJS := $(LINKER_BUILD)/main.o $(LINKER_BUILD)/linker.o \
               $(COMMON_BUILD)/objectfile.o

$(BIN_DIR)/linker: $(LINKER_OBJS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(LINKER_OBJS)

# One .d per object, now one level deeper than before.
-include $(wildcard $(BUILD_DIR)/*/*.d)

# Independent targets sharing one recipe -- each directory is made on its own.
# Not a grouped target (`&:`), which would need make 4.3.
$(BIN_DIR) $(ASEMBLER_BUILD) $(LINKER_BUILD) $(COMMON_BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
