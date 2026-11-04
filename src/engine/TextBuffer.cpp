// TextBuffer.cpp - text edits expressed as whole-line replacements.
#include "TextBuffer.h"

#include <algorithm>

namespace mf
{
    TextBuffer::TextBuffer()
    {
        m_lines.push_back(Line());
    }

    TextPos TextBuffer::Clamp(TextPos pos) const
    {
        pos.line = (std::max)(0, (std::min)(pos.line, LineCount() - 1));
        pos.col = (std::max)(0, (std::min)(pos.col, LineLength(pos.line)));
        return pos;
    }

    TextPos TextBuffer::EndPos() const
    {
        return { LineCount() - 1, LineLength(LineCount() - 1) };
    }

    std::wstring TextBuffer::GetText(TextPos from, TextPos to) const
    {
        from = Clamp(from);
        to = Clamp(to);
        if (to < from)
            std::swap(from, to);
        if (from.line == to.line)
            return LineText(from.line).substr(static_cast<size_t>(from.col), static_cast<size_t>(to.col - from.col));
        std::wstring text = LineText(from.line).substr(static_cast<size_t>(from.col));
        for (int line = from.line; line < to.line; ++line)
        {
            const Eol lineEol = GetLine(line).eol;
            text += EolChars(lineEol == Eol::None ? eol : lineEol);
            if (line + 1 < to.line)
                text += LineText(line + 1);
        }
        text += LineText(to.line).substr(0, static_cast<size_t>(to.col));
        return text;
    }

    void TextBuffer::SetAllText(const std::wstring& text)
    {
        SetAllLines(SplitLines(text));
    }

    void TextBuffer::SetAllLines(Lines lines)
    {
        m_lines = std::move(lines);
        if (m_lines.empty())
            m_lines.push_back(Line());
        for (Line& line : m_lines)
            line.flags = 0;
        eol = DominantEol(m_lines, eol);
    }

    LineEdit TextBuffer::MakeTextEdit(TextPos from, TextPos to, const std::wstring& text, TextPos& end) const
    {
        from = Clamp(from);
        to = Clamp(to);
        if (to < from)
            std::swap(from, to);

        const Line& first = GetLine(from.line);
        const Line& last = GetLine(to.line);
        const std::wstring prefix = first.text.substr(0, static_cast<size_t>(from.col));
        const std::wstring suffix = last.text.substr(static_cast<size_t>(to.col));
        const Eol suffixEol = last.eol;
        const uint8_t flags = static_cast<uint8_t>((first.flags & ~LineResolved) | LineEdited);

        Lines pieces = SplitLines(text);
        LineEdit edit;
        edit.start = from.line;
        edit.removeCount = to.line - from.line + 1;
        const size_t k = pieces.size() - 1;
        for (size_t i = 0; i <= k; ++i)
        {
            Line line;
            line.text = std::move(pieces[i].text);
            line.flags = flags;
            if (i == 0)
                line.comment = first.comment;
            line.eol = eol;
            if (i == 0)
                line.text.insert(0, prefix);
            if (i == k)
            {
                end = { from.line + static_cast<int>(k), static_cast<int>(line.text.size()) };
                line.text += suffix;
                line.eol = suffixEol;
            }
            edit.insert.push_back(std::move(line));
        }
        return edit;
    }

    void TextBuffer::FixupEdit(LineEdit& edit) const
    {
        const int count = LineCount();
        edit.start = (std::max)(0, (std::min)(edit.start, count));
        edit.removeCount = (std::max)(0, (std::min)(edit.removeCount, count - edit.start));
        const bool replacesEnd = edit.start + edit.removeCount == count;
        for (size_t i = 0; i < edit.insert.size(); ++i)
        {
            const bool lastOfFile = replacesEnd && i + 1 == edit.insert.size();
            if (!lastOfFile && edit.insert[i].eol == Eol::None)
                edit.insert[i].eol = eol;
        }
        if (replacesEnd)
        {
            if (edit.insert.empty() && edit.start == 0)
            {
                edit.insert.push_back(Line());
            }
            else if (edit.insert.empty())
            {
                // Removing the end of the file: the line before becomes the last one, so take it into
                // the edit and drop its line ending.
                --edit.start;
                ++edit.removeCount;
                Line line = GetLine(edit.start);
                line.eol = Eol::None;
                edit.insert.push_back(std::move(line));
            }
            else if (edit.insert.back().eol != Eol::None)
            {
                Line empty;
                empty.flags = edit.insert.back().flags;
                edit.insert.push_back(std::move(empty));
            }
        }
    }

    Lines TextBuffer::Apply(const LineEdit& edit)
    {
        auto first = m_lines.begin() + edit.start;
        auto last = first + edit.removeCount;
        Lines removed(std::make_move_iterator(first), std::make_move_iterator(last));
        first = m_lines.erase(first, last);
        m_lines.insert(first, edit.insert.begin(), edit.insert.end());
        return removed;
    }
}
