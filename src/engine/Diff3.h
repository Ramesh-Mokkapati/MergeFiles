// Diff3.h - three-way comparison of two revisions with their common ancestor, and automatic merging.
#pragma once

#include <vector>

#include "Diff.h"

namespace mf
{
    // Chunks for files 0 (left), 1 (ancestor, shown in the middle) and 2 (right), from integer keys
    // made by one LineKeyer. Changes from both sides that overlap or touch form one chunk: BothSame
    // when the two sides made the same change, Conflict otherwise.
    std::vector<Chunk> Diff3Keys(const std::vector<int>& left, const std::vector<int>& base, const std::vector<int>& right);

    std::vector<Chunk> Diff3Lines(const Lines& left, const Lines& base, const Lines& right, const CompareOptions& options);

    struct MergeResult
    {
        Lines lines;
        int merged = 0;     // chunks taken from one side (or both, when the same)
        int conflicts = 0;  // chunks left for manual merging
    };

    // Applies every non-conflicting change to the ancestor. Conflicts keep the ancestor's lines,
    // or with conflictMarkers get "<<<<<<< left / ||||||| base / ======= / >>>>>>> right" blocks.
    // Merged lines get LineEdited | LineResolved.
    MergeResult Merge3(const Lines& left, const Lines& base, const Lines& right, const std::vector<Chunk>& chunks, bool conflictMarkers);
}
