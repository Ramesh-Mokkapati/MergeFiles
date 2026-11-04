// Comparison.cpp - the comparison/merge document behind one tab of the GUI (and the CLI).
#include "Comparison.h"

#include <algorithm>

namespace mf
{
    Comparison::Comparison(int paneCount) : m_paneCount(paneCount == 3 ? 3 : 2)
    {
        for (int pane = 0; pane < 3; ++pane)
            m_buffers[pane].title = L"Untitled";
        Recompare();
    }

    void Comparison::SetPaneCount(int count)
    {
        count = count == 3 ? 3 : 2;
        if (count == m_paneCount)
            return;
        bool modified[3] = {};
        for (int pane = 0; pane < m_paneCount; ++pane)
            modified[pane] = IsModified(pane);
        if (count == 3)
        {
            m_buffers[2] = std::move(m_buffers[1]);
            m_buffers[1] = TextBuffer();
            m_buffers[1].title = L"Untitled (merge)";
            m_buffers[1].eol = m_buffers[0].eol;
            m_buffers[1].encoding = m_buffers[0].encoding;
            modified[2] = modified[1];
            modified[1] = false;
        }
        else
        {
            m_buffers[1] = std::move(m_buffers[2]);
            m_buffers[2] = TextBuffer();
            modified[1] = modified[2];
        }
        m_paneCount = count;
        m_syncPoints.clear();
        // Undo steps refer to panes by position, which just changed.
        m_undo.clear();
        m_redo.clear();
        for (int pane = 0; pane < 3; ++pane)
        {
            m_savedId[pane] = 0;
            m_metaDirty[pane] = modified[pane];
        }
        Recompare();
    }

    void Comparison::SetOptions(const CompareOptions& options)
    {
        m_options = options;
        Recompare();
    }

    bool Comparison::LoadFile(int pane, const std::wstring& path, std::wstring& error, const TextEncoding* forced)
    {
        std::string bytes;
        if (!ReadFileBytes(path, bytes, error))
            return false;
        if (!forced && LooksBinary(bytes))
        {
            error = L"The file appears to be binary, not text.";
            return false;
        }
        DecodedText decoded = DecodeText(bytes, forced);

        bool modified[3] = {};
        for (int p = 0; p < m_paneCount; ++p)
            modified[p] = p != pane && IsModified(p);
        TextBuffer& buffer = Buffer(pane);
        buffer.SetAllText(decoded.text);
        buffer.path = path;
        buffer.encoding = decoded.encoding;
        buffer.lossyDecode = decoded.lossy;
        buffer.readOnly = IsFileReadOnly(path);
        const size_t slash = path.find_last_of(L"\\/");
        buffer.title = slash == std::wstring::npos ? path : path.substr(slash + 1);
        m_syncPoints.clear();
        m_undo.clear();
        m_redo.clear();
        for (int p = 0; p < 3; ++p)
        {
            m_savedId[p] = 0;
            m_metaDirty[p] = modified[p];
        }
        Recompare();
        return true;
    }

    void Comparison::SetText(int pane, const std::wstring& text, const std::wstring& title)
    {
        TextBuffer& buffer = Buffer(pane);
        buffer.SetAllText(text);
        buffer.path.clear();
        buffer.title = title;
        buffer.readOnly = false;
        m_metaDirty[pane] = false;
        m_savedId[pane] = CurrentId(pane);
        Recompare();
    }

    bool Comparison::SaveFile(int pane, const std::wstring& path, std::wstring& error)
    {
        TextBuffer& buffer = Buffer(pane);
        const std::wstring target = path.empty() ? buffer.path : path;
        if (target.empty())
        {
            error = L"The file has no name yet.";
            return false;
        }
        const std::string bytes = EncodeText(buffer.GetAllText(), buffer.encoding);
        if (!WriteFileBytes(target, bytes, error))
            return false;
        if (target != buffer.path)
        {
            buffer.path = target;
            const size_t slash = target.find_last_of(L"\\/");
            buffer.title = slash == std::wstring::npos ? target : target.substr(slash + 1);
            buffer.readOnly = false;
        }
        m_savedId[pane] = CurrentId(pane);
        m_metaDirty[pane] = false;
        ++m_revision;
        return true;
    }

    unsigned Comparison::CurrentId(int pane) const
    {
        for (auto it = m_undo.rbegin(); it != m_undo.rend(); ++it)
        {
            for (const Step& step : it->steps)
            {
                if (step.pane == pane)
                    return it->id;
            }
        }
        return 0;
    }

    bool Comparison::IsModified(int pane) const
    {
        return m_metaDirty[pane] || CurrentId(pane) != m_savedId[pane];
    }

    bool Comparison::AnyModified() const
    {
        for (int pane = 0; pane < m_paneCount; ++pane)
        {
            if (IsModified(pane))
                return true;
        }
        return false;
    }

    void Comparison::SetEncoding(int pane, const TextEncoding& encoding)
    {
        if (Buffer(pane).encoding == encoding)
            return;
        Buffer(pane).encoding = encoding;
        m_metaDirty[pane] = true;
        ++m_revision;
    }

    void Comparison::Changed()
    {
        Recompare();
    }

    void Comparison::Recompare()
    {
        std::vector<const Lines*> files;
        for (int pane = 0; pane < m_paneCount; ++pane)
            files.push_back(&m_buffers[pane].GetLines());
        m_chunks = CompareLines(files, m_options, m_syncPoints);
        ++m_revision;
    }

    ChunkStyle Comparison::StyleInPane(const Chunk& chunk, int pane) const
    {
        if (chunk.kind == ChunkKind::Equal)
            return ChunkStyle::None;
        if (chunk.ignored)
            return PaneDiffersInChunk(chunk, pane) ? ChunkStyle::Ignored : ChunkStyle::None;
        if (chunk.kind == ChunkKind::Conflict)
            return ChunkStyle::Conflict;
        if (!PaneDiffersInChunk(chunk, pane))
            return ChunkStyle::None;
        const int reference = ReferencePane();
        // The other file this pane's chunk is measured against.
        int other = reference == 0 ? 1 : (chunk.kind == ChunkKind::RightOnly ? 2 : 0);
        if (pane != reference)
            other = pane;
        if (chunk.count[reference] == 0)
            return ChunkStyle::Insertion;
        if (chunk.count[other] == 0)
            return ChunkStyle::Removal;
        return ChunkStyle::Change;
    }

    bool Comparison::PaneDiffersInChunk(const Chunk& chunk, int pane) const
    {
        switch (chunk.kind)
        {
        case ChunkKind::Equal:
            return false;
        case ChunkKind::LeftOnly:
            return pane == 0 || pane == 1;
        case ChunkKind::RightOnly:
            return pane == 1 || pane == 2;
        default:
            return true;
        }
    }

    ComparisonStats Comparison::Stats() const
    {
        ComparisonStats stats;
        const int reference = ReferencePane();
        for (const Chunk& chunk : m_chunks)
        {
            if (chunk.kind == ChunkKind::Equal)
                continue;
            if (chunk.ignored)
            {
                ++stats.ignored;
                continue;
            }
            if (chunk.kind == ChunkKind::Conflict)
            {
                ++stats.conflicts;
                continue;
            }
            const int other = !IsThreeWay() ? 1 : (chunk.kind == ChunkKind::RightOnly ? 2 : 0);
            if (chunk.count[reference] == 0)
                ++stats.insertions;
            else if (chunk.count[other] == 0)
                ++stats.removals;
            else
                ++stats.changes;
        }
        return stats;
    }

    int Comparison::ChunkAt(int pane, int line) const
    {
        if (m_chunks.empty())
            return -1;
        // First chunk whose end is past the line (chunks are ordered in every pane).
        auto it = std::upper_bound(m_chunks.begin(), m_chunks.end(), line,
            [pane](int value, const Chunk& chunk) { return value < chunk.End(pane); });
        if (it == m_chunks.end())
            return static_cast<int>(m_chunks.size()) - 1;
        int index = static_cast<int>(it - m_chunks.begin());
        // A difference that is empty in this pane and sits right before the line wins.
        for (int i = index - 1; i >= 0; --i)
        {
            const Chunk& chunk = m_chunks[static_cast<size_t>(i)];
            if (chunk.count[pane] != 0 || chunk.start[pane] != line)
                break;
            if (chunk.IsDifference())
                return i;
        }
        return index;
    }

    int Comparison::FindDifference(int pane, int line, bool forward, bool conflictsOnly) const
    {
        const int count = static_cast<int>(m_chunks.size());
        const int current = ChunkAt(pane, line);
        auto matches = [&](int i) {
            const Chunk& chunk = m_chunks[static_cast<size_t>(i)];
            return conflictsOnly ? chunk.IsConflict() : chunk.IsDifference();
        };
        if (forward)
        {
            for (int i = current + 1; i < count; ++i)
            {
                if (matches(i))
                    return i;
            }
        }
        else
        {
            for (int i = current - 1; i >= 0; --i)
            {
                if (matches(i))
                    return i;
            }
        }
        return -1;
    }

    double Comparison::MapLine(int from, double line, int to) const
    {
        if (from == to || m_chunks.empty())
            return line;
        auto it = std::upper_bound(m_chunks.begin(), m_chunks.end(), line,
            [from](double value, const Chunk& chunk) { return value < chunk.End(from); });
        if (it == m_chunks.end())
        {
            const Chunk& last = m_chunks.back();
            return line - last.End(from) + last.End(to);
        }
        const Chunk& chunk = *it;
        if (chunk.count[from] == 0)
            return chunk.start[to];
        const double fraction = (line - chunk.start[from]) / chunk.count[from];
        return chunk.start[to] + fraction * chunk.count[to];
    }

    // ---- editing ----

    void Comparison::OpenAction(int pane, TextPos caretBefore)
    {
        if (m_openDepth++ == 0)
        {
            m_open = Action();
            m_open.pane = pane;
            m_open.caretBefore = caretBefore;
        }
    }

    void Comparison::CloseAction(TextPos caretAfter, bool typing)
    {
        m_open.caretAfter = caretAfter;
        if (--m_openDepth > 0)
            return;
        if (m_open.steps.empty())
            return;
        m_redo.clear();

        // Merge consecutive typing on one line into one undo step (unless saved in between).
        if (typing && !m_undo.empty())
        {
            Action& last = m_undo.back();
            const bool single = last.steps.size() == 1 && m_open.steps.size() == 1;
            if (last.typing && single && last.pane == m_open.pane && last.caretAfter == m_open.caretBefore && last.id != m_savedId[m_open.pane])
            {
                Step& previous = last.steps[0];
                const Step& next = m_open.steps[0];
                if (previous.edit.start == next.edit.start && previous.edit.insert.size() == 1 && next.edit.removeCount == 1 && next.edit.insert.size() == 1)
                {
                    previous.edit.insert = next.edit.insert;
                    last.caretAfter = caretAfter;
                    last.id = m_nextId++;
                    Changed();
                    return;
                }
            }
        }
        m_open.typing = typing;
        m_open.id = m_nextId++;
        m_undo.push_back(std::move(m_open));
        m_open = Action();
        Changed();
    }

    void Comparison::ApplyEdit(int pane, LineEdit edit)
    {
        TextBuffer& buffer = Buffer(pane);
        buffer.FixupEdit(edit);
        Step step;
        step.pane = pane;
        step.removed = ApplyToBuffer(pane, edit);
        step.edit = std::move(edit);
        m_open.steps.push_back(std::move(step));
    }

    Lines Comparison::ApplyToBuffer(int pane, const LineEdit& edit)
    {
        // Synchronization points follow the lines they were set on.
        const int delta = static_cast<int>(edit.insert.size()) - edit.removeCount;
        for (SyncPoint& point : m_syncPoints)
        {
            int& line = point.line[pane];
            if (line >= edit.start + edit.removeCount)
                line += delta;
            else if (line > edit.start)
                line = edit.start;
        }
        return Buffer(pane).Apply(edit);
    }

    void Comparison::AddSyncPoint(const SyncPoint& point)
    {
        // Keep them ordered; a new link replaces one that shares a line with it.
        for (auto it = m_syncPoints.begin(); it != m_syncPoints.end();)
        {
            bool same = false;
            for (int pane = 0; pane < m_paneCount; ++pane)
                same = same || it->line[pane] == point.line[pane];
            it = same ? m_syncPoints.erase(it) : it + 1;
        }
        m_syncPoints.push_back(point);
        std::sort(m_syncPoints.begin(), m_syncPoints.end(), [](const SyncPoint& a, const SyncPoint& b) { return a.line[0] < b.line[0]; });
        Recompare();
    }

    void Comparison::ClearSyncPoints()
    {
        m_syncPoints.clear();
        Recompare();
    }

    void Comparison::ToggleBookmark(int pane, int line)
    {
        TextBuffer& buffer = Buffer(pane);
        if (line < 0 || line >= buffer.LineCount())
            return;
        buffer.SetLineFlags(line, static_cast<uint8_t>(buffer.GetLine(line).flags ^ LineBookmark));
        if (!(buffer.GetLine(line).flags & LineBookmark))
            buffer.SetLineComment(line, std::wstring());
        ++m_revision;
    }

    void Comparison::SetBookmarkComment(int pane, int line, const std::wstring& comment)
    {
        TextBuffer& buffer = Buffer(pane);
        if (line < 0 || line >= buffer.LineCount())
            return;
        buffer.SetLineFlags(line, static_cast<uint8_t>(buffer.GetLine(line).flags | LineBookmark));
        buffer.SetLineComment(line, comment);
        ++m_revision;
    }

    void Comparison::RestoreLineState(int pane, int line, uint8_t flags, const std::wstring& comment)
    {
        TextBuffer& buffer = Buffer(pane);
        if (line < 0 || line >= buffer.LineCount())
            return;
        buffer.SetLineFlags(line, flags);
        buffer.SetLineComment(line, comment);
        ++m_revision;
    }

    void Comparison::ClearBookmarks()
    {
        for (int pane = 0; pane < m_paneCount; ++pane)
        {
            TextBuffer& buffer = Buffer(pane);
            for (int line = 0; line < buffer.LineCount(); ++line)
            {
                buffer.SetLineFlags(line, static_cast<uint8_t>(buffer.GetLine(line).flags & ~LineBookmark));
                buffer.SetLineComment(line, std::wstring());
            }
        }
        ++m_revision;
    }

    int Comparison::FindBookmark(int pane, int line, bool forward) const
    {
        const TextBuffer& buffer = Buffer(pane);
        const int count = buffer.LineCount();
        for (int step = 1; step <= count; ++step)
        {
            const int candidate = ((forward ? line + step : line - step) % count + count) % count;
            if (buffer.GetLine(candidate).flags & LineBookmark)
                return candidate;
        }
        return -1;
    }

    int Comparison::BookmarkCount() const
    {
        int total = 0;
        for (int pane = 0; pane < m_paneCount; ++pane)
        {
            for (const Line& line : Buffer(pane).GetLines())
                total += (line.flags & LineBookmark) != 0;
        }
        return total;
    }

    void Comparison::BeginGroup(int pane, TextPos caret)
    {
        OpenAction(pane, caret);
    }

    void Comparison::EndGroup()
    {
        CloseAction(m_open.caretAfter, false);
    }

    TextPos Comparison::ReplaceRange(int pane, TextPos from, TextPos to, const std::wstring& text, bool typing)
    {
        TextBuffer& buffer = Buffer(pane);
        TextPos end;
        LineEdit edit = buffer.MakeTextEdit(from, to, text, end);
        OpenAction(pane, (std::min)(from, to));
        ApplyEdit(pane, std::move(edit));
        CloseAction(end, typing);
        return end;
    }

    bool Comparison::CopyChunk(int chunkIndex, int from, int to)
    {
        if (chunkIndex < 0 || chunkIndex >= static_cast<int>(m_chunks.size()) || from == to)
            return false;
        const Chunk chunk = m_chunks[static_cast<size_t>(chunkIndex)];
        const TextBuffer& source = Buffer(from);
        LineEdit edit;
        edit.start = chunk.start[to];
        edit.removeCount = chunk.count[to];
        for (int line = chunk.start[from]; line < chunk.End(from); ++line)
        {
            Line copy = source.GetLine(line);
            copy.flags = LineEdited;
            copy.comment.clear();
            edit.insert.push_back(std::move(copy));
        }
        OpenAction(to, { chunk.start[to], 0 });
        ApplyEdit(to, std::move(edit));
        CloseAction({ chunk.start[to], 0 }, false);
        return true;
    }

    int Comparison::AutoMerge(int& conflicts)
    {
        conflicts = 0;
        if (!IsThreeWay())
            return 0;
        int merged = 0;
        OpenAction(1, TextPos());
        const std::vector<Chunk> chunks = m_chunks;
        for (auto it = chunks.rbegin(); it != chunks.rend(); ++it)
        {
            const Chunk& chunk = *it;
            int from = -1;
            if (chunk.kind == ChunkKind::LeftOnly || chunk.kind == ChunkKind::BothSame)
                from = 0;
            else if (chunk.kind == ChunkKind::RightOnly)
                from = 2;
            else if (chunk.IsConflict())
                ++conflicts;
            if (from < 0)
                continue;
            LineEdit edit;
            edit.start = chunk.start[1];
            edit.removeCount = chunk.count[1];
            for (int line = chunk.start[from]; line < chunk.End(from); ++line)
            {
                Line copy = Buffer(from).GetLine(line);
                copy.flags = LineEdited | LineResolved;
                copy.comment.clear();
                edit.insert.push_back(std::move(copy));
            }
            ApplyEdit(1, std::move(edit));
            ++merged;
        }
        CloseAction(TextPos(), false);
        return merged;
    }

    void Comparison::ReplaceAll(int from, int to)
    {
        if (from == to)
            return;
        LineEdit edit;
        edit.start = 0;
        edit.removeCount = Buffer(to).LineCount();
        edit.insert = Buffer(from).GetLines();
        for (Line& line : edit.insert)
        {
            line.flags = LineEdited;
            line.comment.clear();
        }
        OpenAction(to, TextPos());
        ApplyEdit(to, std::move(edit));
        CloseAction(TextPos(), false);
    }

    void Comparison::Indent(int pane, int firstLine, int lastLine, bool outdent, int tabSize, bool useTabs)
    {
        TextBuffer& buffer = Buffer(pane);
        firstLine = (std::max)(0, firstLine);
        lastLine = (std::min)(lastLine, buffer.LineCount() - 1);
        if (lastLine < firstLine)
            return;
        LineEdit edit;
        edit.start = firstLine;
        edit.removeCount = lastLine - firstLine + 1;
        bool changed = false;
        for (int index = firstLine; index <= lastLine; ++index)
        {
            Line line = buffer.GetLine(index);
            if (outdent)
            {
                size_t remove = 0;
                if (!line.text.empty() && line.text[0] == L'\t')
                    remove = 1;
                else
                {
                    while (remove < line.text.size() && remove < static_cast<size_t>(tabSize) && line.text[remove] == L' ')
                        ++remove;
                }
                if (remove > 0)
                {
                    line.text.erase(0, remove);
                    line.flags |= LineEdited;
                    changed = true;
                }
            }
            else if (!line.text.empty())
            {
                line.text.insert(0, useTabs ? std::wstring(1, L'\t') : std::wstring(static_cast<size_t>(tabSize), L' '));
                line.flags |= LineEdited;
                changed = true;
            }
            edit.insert.push_back(std::move(line));
        }
        if (!changed)
            return;
        OpenAction(pane, { firstLine, 0 });
        ApplyEdit(pane, std::move(edit));
        CloseAction({ firstLine, 0 }, false);
    }

    void Comparison::ConvertLineEndings(int pane, Eol eol)
    {
        TextBuffer& buffer = Buffer(pane);
        buffer.eol = eol;
        Lines lines = buffer.GetLines();
        bool changed = false;
        for (Line& line : lines)
        {
            if (line.eol != Eol::None && line.eol != eol)
            {
                line.eol = eol;
                changed = true;
            }
        }
        if (!changed)
            return;
        LineEdit edit;
        edit.start = 0;
        edit.removeCount = buffer.LineCount();
        edit.insert = std::move(lines);
        OpenAction(pane, TextPos());
        ApplyEdit(pane, std::move(edit));
        CloseAction(TextPos(), false);
    }

    void Comparison::SetResolved(int pane, int firstLine, int lastLine, bool resolved)
    {
        TextBuffer& buffer = Buffer(pane);
        firstLine = (std::max)(0, firstLine);
        lastLine = (std::min)(lastLine, buffer.LineCount() - 1);
        for (int line = firstLine; line <= lastLine; ++line)
        {
            uint8_t flags = buffer.GetLine(line).flags;
            flags = static_cast<uint8_t>(resolved ? (flags | LineResolved) : (flags & ~LineResolved));
            buffer.SetLineFlags(line, flags);
        }
        ++m_revision;
    }

    bool Comparison::Undo(int& pane, TextPos& caret)
    {
        if (m_undo.empty())
            return false;
        Action action = std::move(m_undo.back());
        m_undo.pop_back();
        for (auto it = action.steps.rbegin(); it != action.steps.rend(); ++it)
        {
            LineEdit reverse;
            reverse.start = it->edit.start;
            reverse.removeCount = static_cast<int>(it->edit.insert.size());
            reverse.insert = it->removed;
            ApplyToBuffer(it->pane, reverse);
        }
        pane = action.pane;
        caret = Buffer(pane).Clamp(action.caretBefore);
        m_redo.push_back(std::move(action));
        Changed();
        return true;
    }

    bool Comparison::Redo(int& pane, TextPos& caret)
    {
        if (m_redo.empty())
            return false;
        Action action = std::move(m_redo.back());
        m_redo.pop_back();
        for (const Step& step : action.steps)
            ApplyToBuffer(step.pane, step.edit);
        pane = action.pane;
        caret = Buffer(pane).Clamp(action.caretAfter);
        m_undo.push_back(std::move(action));
        Changed();
        return true;
    }
}
