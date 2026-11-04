// Search.h - finding text in a buffer (single-line search strings).
#pragma once

#include <string>

#include "TextBuffer.h"

namespace mf
{
    struct SearchOptions
    {
        bool matchCase = false;
        bool wholeWord = false;
        bool backward = false;
        bool wrap = true;
    };

    // Searches from `from` (forwards: the match starts at or after it; backwards: it ends at or
    // before it). With wrap, continues from the other end of the buffer. Restricting the search to
    // [rangeStart, rangeEnd) is optional (pass rangeEnd == rangeStart for the whole buffer).
    bool FindInBuffer(const TextBuffer& buffer, const std::wstring& what, TextPos from, const SearchOptions& options,
        TextPos& matchStart, TextPos& matchEnd, TextPos rangeStart = TextPos(), TextPos rangeEnd = TextPos());

    // Finds a match in one line at or after (or, backwards, ending at or before) col; -1 if none.
    int FindInLine(const std::wstring& line, const std::wstring& what, int col, const SearchOptions& options);
}
