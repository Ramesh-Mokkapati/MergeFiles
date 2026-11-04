// Patch.cpp - unified diff parsing and application.
#include "Patch.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>

namespace mf
{
    namespace
    {
        bool StartsWith(const std::wstring& text, const wchar_t* prefix)
        {
            return text.compare(0, wcslen(prefix), prefix) == 0;
        }

        // "--- a/x.cpp\t2026-01-01 ..." -> "a/x.cpp"; quoted git paths lose their quotes.
        std::wstring HeaderPath(const std::wstring& line)
        {
            std::wstring path = line.substr(4);
            const size_t tab = path.find(L'\t');
            if (tab != std::wstring::npos)
                path.erase(tab);
            while (!path.empty() && path.back() == L' ')
                path.pop_back();
            if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"')
                path = path.substr(1, path.size() - 2);
            return path;
        }

        // "@@ -12,5 +12,7 @@ section"
        bool ParseHunkHeader(const std::wstring& line, Hunk& hunk)
        {
            int oldStart = 0, oldCount = 1, newStart = 0, newCount = 1;
            const wchar_t* p = line.c_str() + 3;
            wchar_t* end = nullptr;
            if (*p != L'-')
                return false;
            oldStart = static_cast<int>(wcstol(p + 1, &end, 10));
            p = end;
            if (*p == L',')
            {
                oldCount = static_cast<int>(wcstol(p + 1, &end, 10));
                p = end;
            }
            while (*p == L' ')
                ++p;
            if (*p != L'+')
                return false;
            newStart = static_cast<int>(wcstol(p + 1, &end, 10));
            p = end;
            if (*p == L',')
            {
                newCount = static_cast<int>(wcstol(p + 1, &end, 10));
                p = end;
            }
            const wchar_t* close = wcsstr(p, L"@@");
            if (!close)
                return false;
            hunk.oldStart = oldStart;
            hunk.oldCount = oldCount;
            hunk.newStart = newStart;
            hunk.newCount = newCount;
            hunk.section = close + 2;
            while (!hunk.section.empty() && hunk.section.front() == L' ')
                hunk.section.erase(0, 1);
            return true;
        }

        std::wstring TrimRight(const std::wstring& text)
        {
            size_t end = text.size();
            while (end > 0 && (text[end - 1] == L' ' || text[end - 1] == L'\t' || text[end - 1] == L'\r'))
                --end;
            return text.substr(0, end);
        }

        bool MatchAt(const std::vector<std::wstring>& lines, int at, const std::vector<std::wstring>& wanted, bool fuzzy)
        {
            if (at < 0 || at + static_cast<int>(wanted.size()) > static_cast<int>(lines.size()))
                return false;
            for (size_t k = 0; k < wanted.size(); ++k)
            {
                const std::wstring& have = lines[static_cast<size_t>(at) + k];
                if (fuzzy ? TrimRight(have) != TrimRight(wanted[k]) : have != wanted[k])
                    return false;
            }
            return true;
        }
    }

    int Hunk::Added() const
    {
        return static_cast<int>(std::count_if(lines.begin(), lines.end(), [](const PatchLine& line) { return line.op == L'+'; }));
    }

    int Hunk::Removed() const
    {
        return static_cast<int>(std::count_if(lines.begin(), lines.end(), [](const PatchLine& line) { return line.op == L'-'; }));
    }

    std::vector<FilePatch> ParsePatch(const std::wstring& text, std::wstring& error)
    {
        std::vector<FilePatch> patches;
        const Lines lines = SplitLines(text);
        size_t i = 0;
        while (i < lines.size())
        {
            const std::wstring& line = lines[i].text;
            if (!(StartsWith(line, L"--- ") && i + 1 < lines.size() && StartsWith(lines[i + 1].text, L"+++ ")))
            {
                ++i;
                continue;
            }
            FilePatch file;
            file.oldPath = HeaderPath(line);
            file.newPath = HeaderPath(lines[i + 1].text);
            file.isNew = file.oldPath == L"/dev/null";
            file.isDeleted = file.newPath == L"/dev/null";
            i += 2;
            while (i < lines.size() && StartsWith(lines[i].text, L"@@ "))
            {
                Hunk hunk;
                if (!ParseHunkHeader(lines[i].text, hunk))
                {
                    error = L"Unreadable hunk header: " + lines[i].text;
                    return patches;
                }
                ++i;
                int oldLeft = hunk.oldCount, newLeft = hunk.newCount;
                while (i < lines.size() && (oldLeft > 0 || newLeft > 0))
                {
                    const std::wstring& body = lines[i].text;
                    if (StartsWith(body, L"\\"))
                    {
                        if (!hunk.lines.empty())
                            hunk.lines.back().noNewline = true;
                        ++i;
                        continue;
                    }
                    // An empty line in a hunk is a context line whose leading space was trimmed.
                    const wchar_t op = body.empty() ? L' ' : body[0];
                    if (op != L' ' && op != L'-' && op != L'+')
                        break;
                    hunk.lines.push_back({ op, body.empty() ? std::wstring() : body.substr(1), false });
                    if (op != L'+')
                        --oldLeft;
                    if (op != L'-')
                        --newLeft;
                    ++i;
                }
                if (i < lines.size() && StartsWith(lines[i].text, L"\\") && !hunk.lines.empty())
                {
                    hunk.lines.back().noNewline = true;
                    ++i;
                }
                if (oldLeft > 0 || newLeft > 0)
                {
                    error = L"A hunk in " + file.newPath + L" is shorter than its header says.";
                    file.hunks.push_back(std::move(hunk));
                    patches.push_back(std::move(file));
                    return patches;
                }
                file.hunks.push_back(std::move(hunk));
            }
            patches.push_back(std::move(file));
        }
        if (patches.empty() && error.empty())
            error = L"No unified diff (--- / +++ / @@ lines) found.";
        return patches;
    }

    std::wstring StripPatchPath(const std::wstring& path, int strip)
    {
        if (path == L"/dev/null")
            return std::wstring();
        std::wstring result = path;
        for (int k = 0; k < strip; ++k)
        {
            const size_t slash = result.find_first_of(L"/\\");
            if (slash == std::wstring::npos)
                break;
            result.erase(0, slash + 1);
        }
        std::replace(result.begin(), result.end(), L'/', L'\\');
        return result;
    }

    std::wstring PatchTarget(const FilePatch& patch, int strip)
    {
        return StripPatchPath(patch.isDeleted ? patch.oldPath : patch.newPath, strip);
    }

    std::wstring ApplyPatch(const std::wstring& original, const FilePatch& patch, const std::vector<bool>& selected, std::vector<HunkResult>& results)
    {
        // Work on the text lines; remember the line ending and whether the file ends with one.
        Lines split = SplitLines(original);
        const Eol eol = DominantEol(split, Eol::CrLf);
        bool finalNewline = split.size() > 1 && split.back().text.empty();
        if (finalNewline || (split.size() == 1 && split.back().text.empty()))
            split.pop_back();
        std::vector<std::wstring> lines;
        for (const Line& line : split)
            lines.push_back(line.text);

        results.assign(patch.hunks.size(), HunkResult());
        int delta = 0;
        for (size_t h = 0; h < patch.hunks.size(); ++h)
        {
            const Hunk& hunk = patch.hunks[h];
            HunkResult& result = results[h];
            if (!selected.empty() && !selected[h])
            {
                result.skipped = true;
                continue;
            }
            std::vector<std::wstring> oldLines, newLines;
            bool newEndsWithoutNewline = false;
            for (const PatchLine& line : hunk.lines)
            {
                if (line.op != L'+')
                    oldLines.push_back(line.text);
                if (line.op != L'-')
                {
                    newLines.push_back(line.text);
                    newEndsWithoutNewline = line.noNewline;
                }
            }
            // Where the header says, then ever further away; exact before ignoring trailing spaces.
            const int expected = (hunk.oldCount == 0 ? hunk.oldStart : hunk.oldStart - 1) + delta;
            int found = -1;
            bool fuzzy = false;
            const int limit = static_cast<int>(lines.size()) + 1;
            for (int pass = 0; pass < 2 && found < 0; ++pass)
            {
                for (int distance = 0; distance <= limit && found < 0; ++distance)
                {
                    for (int sign : { 1, -1 })
                    {
                        const int at = expected + sign * distance;
                        if (MatchAt(lines, at, oldLines, pass == 1))
                        {
                            found = at;
                            fuzzy = pass == 1;
                            break;
                        }
                        if (distance == 0)
                            break;
                    }
                }
            }
            if (found < 0)
                continue;
            lines.erase(lines.begin() + found, lines.begin() + found + static_cast<long long>(oldLines.size()));
            lines.insert(lines.begin() + found, newLines.begin(), newLines.end());
            // The end of the file: "\ No newline at end of file" decides the final line break.
            if (!newLines.empty() && found + static_cast<int>(newLines.size()) == static_cast<int>(lines.size()))
                finalNewline = !newEndsWithoutNewline;
            result.applied = true;
            result.fuzzy = fuzzy;
            result.offset = found - expected;
            result.line = found + 1;
            delta += static_cast<int>(newLines.size()) - static_cast<int>(oldLines.size()) + result.offset;
        }

        std::wstring text;
        for (size_t k = 0; k < lines.size(); ++k)
        {
            text += lines[k];
            if (k + 1 < lines.size() || finalNewline)
                text += EolChars(eol);
        }
        return text;
    }

    FilePatch ReversePatch(const FilePatch& patch)
    {
        FilePatch reversed = patch;
        std::swap(reversed.oldPath, reversed.newPath);
        std::swap(reversed.isNew, reversed.isDeleted);
        for (Hunk& hunk : reversed.hunks)
        {
            std::swap(hunk.oldStart, hunk.newStart);
            std::swap(hunk.oldCount, hunk.newCount);
            for (PatchLine& line : hunk.lines)
            {
                if (line.op == L'+')
                    line.op = L'-';
                else if (line.op == L'-')
                    line.op = L'+';
            }
        }
        return reversed;
    }
}
