#pragma once

// The memory image format: the only contract between linker and emulator.
// One writer and one reader, both here, for the same reason objectfile.hpp
// holds the object file format -- so the layout is written down exactly once
// and a round trip through it cannot drift.

#include <cstdint>
#include <map>
#include <string>

namespace hexfile {

// Sparse by construction: only the addresses a link actually filled appear,
// which is what lets the record skip the gaps between placed sections.
// Ordered, so the writer can walk rows in address order.
using Image = std::map<std::uint32_t, std::uint8_t>;

// False only if the output file could not be opened; the caller words the error.
bool write(const std::string &path, const Image &image);

// Throws std::runtime_error, worded with the offending line, on anything
// malformed -- as objfile::read does, and for the same reason: the file is
// machine-generated, so a deviation is a bug rather than something to recover from.
Image read(const std::string &path);

} // namespace hexfile
