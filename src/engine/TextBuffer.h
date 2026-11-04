// TextBuffer.h - the editable text of one compared file, with its encoding and line endings.
// Every change is a LineEdit (replace a run of whole lines), which makes undo simple and exact.
#pragma once

#include <string>

#include "Encoding.h"
#include "TextLines.h"

namespace mf
{
    struct TextPos
    {
        int line = 0;
        int col = 0;

        bool operator==(const TextPos& o) const
        {
            return line == o.line && col == o.col;
        }
        bool operator!=(const TextPos& o) const
        {
            return !(*this == o);
        }
        bool operator<(const TextPos& o) const
        {
            return line < o.line || (line == o.line && col < o.col);
        }
        bool operator<=(const TextPos& o) const
        {
            return !(o < *this);
        }
    };

    // Replace lines [start, start + removeCount) with insert.
    struct LineEdit
    {
        int start = 0;
        int removeCount = 0;
        Lines insert;
    };

    class TextBuffer
    {
    public:
        TextBuffer();

        const Lines& GetLines() const
        {
            return m_lines;
        }
        int LineCount() const
        {
            return static_cast<int>(m_lines.size());
        }
        const Line& GetLine(int line) const
        {
            return m_lines[static_cast<size_t>(line)];
        }
        const std::wstring& LineText(int line) const
        {
            return m_lines[static_cast<size_t>(line)].text;
        }
        int LineLength(int line) const
        {
            return static_cast<int>(m_lines[static_cast<size_t>(line)].text.size());
        }

        TextPos Clamp(TextPos pos) const;
        TextPos EndPos() const;
        // Text between two positions; line breaks are each line's own (or the default for the last).
        std::wstring GetText(TextPos from, TextPos to) const;
        std::wstring GetAllText() const
        {
            return JoinLines(m_lines);
        }

        // Replaces all text without undo (loading). Clears every line flag.
        void SetAllText(const std::wstring& text);
        void SetAllLines(Lines lines);

        // The LineEdit that replaces [from, to) by text (whose line breaks become eol). end receives
        // the position just after the inserted text. New lines get LineEdited.
        LineEdit MakeTextEdit(TextPos from, TextPos to, const std::wstring& text, TextPos& end) const;

        // Keeps the buffer's invariants when edit is applied: at least one line, the last line has no
        // line ending, every other line has one.
        void FixupEdit(LineEdit& edit) const;

        // Applies an edit; returns the lines it removed.
        Lines Apply(const LineEdit& edit);

        void SetLineFlags(int line, uint8_t flags)
        {
            m_lines[static_cast<size_t>(line)].flags = flags;
        }
        void SetLineComment(int line, const std::wstring& comment)
        {
            m_lines[static_cast<size_t>(line)].comment = comment;
        }

        // File properties.
        std::wstring path;       // empty for an untitled buffer
        std::wstring title;      // shown when there is no path
        TextEncoding encoding;   // used when saving
        Eol eol = Eol::CrLf;     // line ending for new lines
        bool readOnly = false;   // file attribute; editing is refused
        bool lossyDecode = false;

    private:
        Lines m_lines;
    };
}
