// BinaryDiff.h - byte-level comparison of two files.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Diff.h"

namespace mf
{
    struct BinaryDiff
    {
        // Chunks over bytes (start/count are byte offsets and lengths; files 0 and 1).
        std::vector<Chunk> chunks;
        // True when bytes were compared offset by offset (same size, or too large for a
        // byte-level diff): insertions and removals then show as changes up to the end.
        bool aligned = false;
        uint64_t differentBytes = 0;  // bytes inside difference chunks, on the larger side
        int Differences() const;
    };

    // Compares two byte strings. Files of equal size, or larger together than diffLimit bytes, are
    // compared offset by offset; otherwise a byte-level LCS diff finds insertions and removals.
    BinaryDiff DiffBytes(const std::string& a, const std::string& b, size_t diffLimit = 1024 * 1024);
}
