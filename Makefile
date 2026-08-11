# Root build for the whole toolchain: asembler (later: linker, emulator).
#
#   make            build bin/asembler
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

.PHONY: all parser lexer clean help

all: $(BIN_DIR)/asembler

help:
	@echo "Available targets:"
	@echo "  make            - build bin/asembler (default)"
	@echo "  make parser     - generate + compile the bison parser only, no link"
	@echo "                    (catches grammar conflicts and C++ errors in actions fast)"
	@echo "  make lexer      - generate + compile the flex lexer only, no link"
	@echo "  make clean      - remove build/ and bin/"
	@echo "  make help       - show this message"

# --- asembler ---------------------------------------------------------
# Grammar sources live in misc/ (see Project_specs.md: flex/bison inputs
# belong there, their generated output must never be submitted).
$(BUILD_DIR)/parser.tab.c: misc/parser.y | $(BUILD_DIR)
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
$(BUILD_DIR)/parser.tab.h: $(BUILD_DIR)/parser.tab.c ;

$(BUILD_DIR)/lex.yy.c: misc/lexer.l | $(BUILD_DIR)
	$(FLEX) -o $@ $<

# Compiled separately (rather than fed straight to the final link command)
# so `make parser`/`make lexer` can surface C++ errors in grammar actions
# without waiting on the rest of the build, and so unrelated files don't
# get recompiled on every change.
$(BUILD_DIR)/parser.tab.o: $(BUILD_DIR)/parser.tab.c | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -I$(BUILD_DIR) -c -o $@ $<

# lex.yy.c #includes parser.tab.h (for the token codes), so it must exist
# before this compiles -- not just lex.yy.c itself.
$(BUILD_DIR)/lex.yy.o: $(BUILD_DIR)/lex.yy.c $(BUILD_DIR)/parser.tab.h | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -I$(BUILD_DIR) -c -o $@ $<

parser: $(BUILD_DIR)/parser.tab.o

lexer: $(BUILD_DIR)/lex.yy.o

$(BUILD_DIR)/%.o: src/asembler/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INC_DIR) -I$(BUILD_DIR) -c -o $@ $<

# main.cpp #includes parser.tab.h directly.
$(BUILD_DIR)/main.o: $(BUILD_DIR)/parser.tab.h

ASEMBLER_OBJS := $(BUILD_DIR)/main.o $(BUILD_DIR)/assembler.o \
                  $(BUILD_DIR)/parser.tab.o $(BUILD_DIR)/lex.yy.o

$(BIN_DIR)/asembler: $(ASEMBLER_OBJS) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $(ASEMBLER_OBJS)

-include $(wildcard $(BUILD_DIR)/*.d)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
