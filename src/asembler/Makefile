CXX := g++
CXXFLAGS := -Wall -std=c++17
BIN_DIR := ../../bin
TARGET := $(BIN_DIR)/asembler
SRC := asembler.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -f $(TARGET)
