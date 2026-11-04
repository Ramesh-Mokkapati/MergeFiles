// InlineDiff.h - differences within lines: which characters of two corresponding lines changed,
// and which lines of a changed block correspond to each other.
#pragma once

#include <string>
#include <utility>
#include <vector>

#include "Diff.h"

namespace mf
{
    struct CharRange
    {
        int start = 0;
        int length = 0;
    };

    struct InlineDiff
    {
        std::vector<CharRange> a;  // changed characters of the first line
        std::vector<CharRange> b;  // changed characters of the second line
        double similarity = 0;     // 0 (nothing in common) .. 1 (equal)
    };

    // Compares two lines token by token (words, runs of whitespace, single symbols), so whole
    // identifiers are highlighted rather than scattered letters.
    InlineDiff DiffLineChars(const std::wstring& a, const std::wstring& b, const CompareOptions& options);

    // Pairs the lines of a changed block (a against b) that correspond, in order. Lines that are too
    // different stay unpaired (shown as wholly inserted or removed).
    std::vector<std::pair<int, int>> PairLines(const std::vector<const std::wstring*>& a, const std::vector<const std::wstring*>& b, const CompareOptions& options);
}
