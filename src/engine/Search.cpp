// Search.cpp - plain-text search with case and whole-word options.
#include "Search.h"

#include <algorithm>
#include <cwctype>

namespace mf
{
    namespace
    {
        bool IsWordChar(wchar_t c)
        {
            return c == L'_' || std::iswalnum(c);
        }

        bool MatchesAt(const std::wstring& line, const std::wstring& what, size_t at, const SearchOptions& options)
        {
            if (at + what.size() > line.size())
                return false;
            for (size_t k = 0; k < what.size(); ++k)
            {
                wchar_t a = line[at + k];
                wchar_t b = what[k];
                if (!options.matchCase)
                {
                    a = static_cast<wchar_t>(std::towlower(a));
                    b = static_cast<wchar_t>(std::towlower(b));
                }
                if (a != b)
                    return false;
            }
            if (options.wholeWord)
            {
                if (at > 0 && IsWordChar(line[at - 1]) && IsWordChar(what.front()))
                    return false;
                const size_t end = at + what.size();
                if (end < line.size() && IsWordChar(line[end]) && IsWordChar(what.back()))
                    return false;
            }
            return true;
        }
    }

    int FindInLine(const std::wstring& line, const std::wstring& what, int col, const SearchOptions& options)
    {
        if (what.empty() || what.size() > line.size())
            return -1;
        const int last = static_cast<int>(line.size() - what.size());
        if (options.backward)
        {
            // The match must end at or before col.
            for (int at = (std::min)(last, col - static_cast<int>(what.size())); at >= 0; --at)
            {
                if (MatchesAt(line, what, static_cast<size_t>(at), options))
                    return at;
            }
            return -1;
        }
        for (int at = (std::max)(0, col); at <= last; ++at)
        {
            if (MatchesAt(line, what, static_cast<size_t>(at), options))
                return at;
        }
        return -1;
    }

    bool FindInBuffer(const TextBuffer& buffer, const std::wstring& what, TextPos from, const SearchOptions& options,
        TextPos& matchStart, TextPos& matchEnd, TextPos rangeStart, TextPos rangeEnd)
    {
        if (what.empty() || what.find_first_of(L"\r\n") != std::wstring::npos)
            return false;
        const bool limited = rangeStart != rangeEnd;
        if (!limited)
        {
            rangeStart = TextPos();
            rangeEnd = buffer.EndPos();
        }
        from = buffer.Clamp(from);
        if (from < rangeStart)
            from = options.backward ? rangeEnd : rangeStart;
        if (rangeEnd < from)
            from = options.backward ? rangeEnd : rangeStart;
        const int width = static_cast<int>(what.size());
        const int lines = rangeEnd.line - rangeStart.line + 1;

        // Visit each line of the range once (plus the starting line again after wrapping).
        for (int pass = 0; pass <= lines; ++pass)
        {
            int line;
            int col;
            if (!options.backward)
            {
                line = from.line + pass;
                if (line > rangeEnd.line)
                {
                    if (!options.wrap)
                        return false;
                    line -= lines;
                }
                col = pass == 0 ? from.col : (line == rangeStart.line ? rangeStart.col : 0);
            }
            else
            {
                line = from.line - pass;
                if (line < rangeStart.line)
                {
                    if (!options.wrap)
                        return false;
                    line += lines;
                }
                col = pass == 0 ? from.col : (line == rangeEnd.line ? rangeEnd.col : buffer.LineLength(line));
            }
            if (pass == lines && !options.backward)
                col = line == rangeStart.line ? rangeStart.col : 0;
            const std::wstring& text = buffer.LineText(line);
            int at = FindInLine(text, what, col, options);
            while (at >= 0)
            {
                const TextPos start = { line, at };
                const TextPos end = { line, at + width };
                if (!(start < rangeStart) && !(rangeEnd < end))
                {
                    matchStart = start;
                    matchEnd = end;
                    return true;
                }
                at = options.backward ? FindInLine(text, what, at + width - 1, options) : FindInLine(text, what, at + 1, options);
            }
        }
        return false;
    }
}
