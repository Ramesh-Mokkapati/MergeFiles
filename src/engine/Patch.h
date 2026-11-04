// Patch.h - unified diff ("patch") files: parsing (including git diffs), and applying chosen
// hunks to a file, finding each hunk's context even when the file has moved on.
#pragma once

#include <string>
#include <vector>

#include "TextLines.h"

namespace mf
{
    struct PatchLine
    {
        wchar_t op = L' ';  // ' ' context, '-' removed, '+' added
        std::wstring text;
        bool noNewline = false;  // followed by "\ No newline at end of file"
    };

    struct Hunk
    {
        int oldStart = 0;  // 1-based, as written in the @@ header
        int oldCount = 0;
        int newStart = 0;
        int newCount = 0;
        std::wstring section;  // text after the second @@ (often a function name)
        std::vector<PatchLine> lines;

        int Added() const;
        int Removed() const;
    };

    struct FilePatch
    {
        std::wstring oldPath;  // as written, e.g. "a/src/main.cpp" or "/dev/null"
        std::wstring newPath;
        bool isNew = false;
        bool isDeleted = false;
        std::vector<Hunk> hunks;
    };

    // Parses unified diffs (diff -u, git diff, svn diff). Lines before and between files are ignored.
    std::vector<FilePatch> ParsePatch(const std::wstring& text, std::wstring& error);

    // Removes `strip` leading path components ("a/src/x.cpp" with 1 -> "src/x.cpp"); "/dev/null" -> "".
    std::wstring StripPatchPath(const std::wstring& path, int strip);
    // The path a file patch applies to (the new path, or the old one for a deletion), stripped.
    std::wstring PatchTarget(const FilePatch& patch, int strip);

    struct HunkResult
    {
        bool applied = false;
        bool skipped = false;   // not selected
        int offset = 0;         // lines away from where the header said
        int line = 0;           // 1-based line in the result where the hunk now starts
        bool fuzzy = false;     // matched only when ignoring trailing whitespace
    };

    // Applies the selected hunks (selected.size() == hunks.size(), or empty for all) to a file's
    // text. Each hunk's old lines are searched nearest to where its header says first. New lines get
    // the file's line ending.
    std::wstring ApplyPatch(const std::wstring& original, const FilePatch& patch, const std::vector<bool>& selected, std::vector<HunkResult>& results);

    // Reverses a patch (to undo it, or review it the other way round).
    FilePatch ReversePatch(const FilePatch& patch);
}
