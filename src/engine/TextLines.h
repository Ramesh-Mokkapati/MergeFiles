// TextLines.h - a text file as a list of lines, each with its own line ending, so files with
// mixed line endings save back exactly as they were read.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mf
{
    enum class Eol : uint8_t
    {
        None,  // the last line of a file that doesn't end with a line break
        CrLf,  // Windows
        Lf,    // UNIX
        Cr     // classic Mac
    };

    // Per-line state kept by the editor (not part of the file contents).
    enum LineFlags : uint8_t
    {
        LineEdited = 1,    // edit marker: typed into, or copied from another file
        LineResolved = 2,  // dimmed: marked as resolved, or merged automatically
        LineBookmark = 4,  // bookmark (kept with the line through edits)
    };

    struct Line
    {
        std::wstring text;
        Eol eol = Eol::None;
        uint8_t flags = 0;
        std::wstring comment;  // bookmark comment (kept with the line through edits)
    };

    using Lines = std::vector<Line>;

    const wchar_t* EolChars(Eol eol);  // L"\r\n", L"\n", L"\r" or L""
    const wchar_t* EolName(Eol eol);   // L"CR LF", L"LF", L"CR" or L"None"

    // Always returns at least one line. The last line has Eol::None; it is empty when the text ends
    // with a line break, so SplitLines("a\n") is { "a" LF, "" }.
    Lines SplitLines(const std::wstring& text);
    // Exact inverse of SplitLines.
    std::wstring JoinLines(const Lines& lines);

    // The most common line ending, or fallback when no line has one.
    Eol DominantEol(const Lines& lines, Eol fallback = Eol::CrLf);
    // True when the lines use more than one kind of line ending.
    bool HasMixedEol(const Lines& lines);
    // Gives every line that has a line ending the given one.
    void ConvertEol(Lines& lines, Eol eol);
}
