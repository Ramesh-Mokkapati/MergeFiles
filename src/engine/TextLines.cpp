// TextLines.cpp - splitting text into lines and joining it back.
#include "TextLines.h"

namespace mf
{
    const wchar_t* EolChars(Eol eol)
    {
        switch (eol)
        {
        case Eol::CrLf:
            return L"\r\n";
        case Eol::Lf:
            return L"\n";
        case Eol::Cr:
            return L"\r";
        default:
            return L"";
        }
    }

    const wchar_t* EolName(Eol eol)
    {
        switch (eol)
        {
        case Eol::CrLf:
            return L"CR LF";
        case Eol::Lf:
            return L"LF";
        case Eol::Cr:
            return L"CR";
        default:
            return L"None";
        }
    }

    Lines SplitLines(const std::wstring& text)
    {
        Lines lines;
        size_t start = 0;
        const size_t n = text.size();
        for (size_t i = 0; i < n; ++i)
        {
            const wchar_t c = text[i];
            if (c != L'\n' && c != L'\r')
                continue;
            Line line;
            line.text.assign(text, start, i - start);
            if (c == L'\r' && i + 1 < n && text[i + 1] == L'\n')
            {
                line.eol = Eol::CrLf;
                ++i;
            }
            else
            {
                line.eol = c == L'\n' ? Eol::Lf : Eol::Cr;
            }
            lines.push_back(std::move(line));
            start = i + 1;
        }
        Line last;
        last.text.assign(text, start, n - start);
        lines.push_back(std::move(last));
        return lines;
    }

    std::wstring JoinLines(const Lines& lines)
    {
        size_t total = 0;
        for (const Line& line : lines)
            total += line.text.size() + 2;
        std::wstring text;
        text.reserve(total);
        for (const Line& line : lines)
        {
            text += line.text;
            text += EolChars(line.eol);
        }
        return text;
    }

    Eol DominantEol(const Lines& lines, Eol fallback)
    {
        size_t counts[4] = {};
        for (const Line& line : lines)
            ++counts[static_cast<int>(line.eol)];
        Eol best = fallback;
        size_t bestCount = 0;
        for (Eol eol : { Eol::CrLf, Eol::Lf, Eol::Cr })
        {
            if (counts[static_cast<int>(eol)] > bestCount)
            {
                best = eol;
                bestCount = counts[static_cast<int>(eol)];
            }
        }
        return best;
    }

    bool HasMixedEol(const Lines& lines)
    {
        Eol seen = Eol::None;
        for (const Line& line : lines)
        {
            if (line.eol == Eol::None)
                continue;
            if (seen != Eol::None && line.eol != seen)
                return true;
            seen = line.eol;
        }
        return false;
    }

    void ConvertEol(Lines& lines, Eol eol)
    {
        for (Line& line : lines)
        {
            if (line.eol != Eol::None)
                line.eol = eol;
        }
    }
}
