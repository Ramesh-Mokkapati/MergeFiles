// Comparison.h - two or three files being compared and merged: loading and saving, the current
// chunks, every editing operation (typing, point-and-click merging, automatic merging, indenting)
// and one undo history shared by all panes.
#pragma once

#include <string>
#include <vector>

#include "Diff.h"
#include "Diff3.h"
#include "TextBuffer.h"

namespace mf
{
    struct ComparisonStats
    {
        int insertions = 0;  // lines only in the non-reference file
        int removals = 0;    // lines only in the reference file
        int changes = 0;
        int conflicts = 0;   // three-way only
        int ignored = 0;     // blocks that differ only in ignored ways (not counted as differences)
        int Differences() const
        {
            return insertions + removals + changes + conflicts;
        }
    };

    // How a chunk looks in one pane (relative to the reference pane: the left file in a two-way
    // comparison, the middle file in a three-way one).
    enum class ChunkStyle
    {
        None,
        Insertion,
        Removal,
        Change,
        Conflict,
        Ignored  // differs only in ways the options ignore
    };

    class Comparison
    {
    public:
        explicit Comparison(int paneCount = 2);

        int PaneCount() const
        {
            return m_paneCount;
        }
        bool IsThreeWay() const
        {
            return m_paneCount == 3;
        }
        // Two-way: pane 0. Three-way: the middle pane (the common ancestor, which merging changes).
        int ReferencePane() const
        {
            return IsThreeWay() ? 1 : 0;
        }
        // Going from 2 to 3 panes adds an empty middle pane; from 3 to 2 removes the middle pane.
        void SetPaneCount(int count);

        TextBuffer& Buffer(int pane)
        {
            return m_buffers[static_cast<size_t>(pane)];
        }
        const TextBuffer& Buffer(int pane) const
        {
            return m_buffers[static_cast<size_t>(pane)];
        }

        const CompareOptions& Options() const
        {
            return m_options;
        }
        void SetOptions(const CompareOptions& options);

        // ---- files ----
        bool LoadFile(int pane, const std::wstring& path, std::wstring& error, const TextEncoding* forced = nullptr);
        void SetText(int pane, const std::wstring& text, const std::wstring& title);
        // Saves to path, or to the pane's own path when path is empty.
        bool SaveFile(int pane, const std::wstring& path, std::wstring& error);
        bool IsModified(int pane) const;
        bool AnyModified() const;
        void SetEncoding(int pane, const TextEncoding& encoding);

        // ---- comparison ----
        void Recompare();
        const std::vector<Chunk>& Chunks() const
        {
            return m_chunks;
        }
        ComparisonStats Stats() const;
        ChunkStyle StyleInPane(const Chunk& chunk, int pane) const;
        // Index of the chunk containing line (a chunk that is empty in this pane "contains" the line
        // it sits before), or -1.
        int ChunkAt(int pane, int line) const;
        // The next (or previous) difference chunk after (before) the chunk at line; -1 if none.
        int FindDifference(int pane, int line, bool forward, bool conflictsOnly) const;
        // The line in pane `to` that corresponds to (fractional) line `line` of pane `from`.
        double MapLine(int from, double line, int to) const;
        // The panes whose text differs within a chunk, used by the copy commands and the arrows.
        bool PaneDiffersInChunk(const Chunk& chunk, int pane) const;

        // ---- editing (each call is one undo step, followed by a recompare) ----
        // Replaces [from, to) of a pane with text; returns the position after the new text.
        // Consecutive typing on one line is merged into a single undo step.
        TextPos ReplaceRange(int pane, TextPos from, TextPos to, const std::wstring& text, bool typing = false);
        // Point-and-click merge: makes chunk's lines in pane `to` a copy of those in pane `from`.
        bool CopyChunk(int chunkIndex, int from, int to);
        // Three-way: merges every non-conflicting change into the middle pane. Returns the number
        // of chunks merged; conflicts receives the number left for manual merging.
        int AutoMerge(int& conflicts);
        // Replaces the whole text of `to` with that of `from` (Replace File command).
        void ReplaceAll(int from, int to);
        void Indent(int pane, int firstLine, int lastLine, bool outdent, int tabSize, bool useTabs);
        void ConvertLineEndings(int pane, Eol eol);
        // Dims lines (not part of undo; does not modify the file).
        void SetResolved(int pane, int firstLine, int lastLine, bool resolved);

        // ---- synchronization links: force lines of the files to correspond ----
        void AddSyncPoint(const SyncPoint& point);
        void ClearSyncPoints();
        const std::vector<SyncPoint>& SyncPoints() const
        {
            return m_syncPoints;
        }

        // ---- bookmarks (line flags; not part of undo, do not modify the file) ----
        void ToggleBookmark(int pane, int line);
        // Sets a bookmark with a comment (an empty comment keeps a plain bookmark).
        void SetBookmarkComment(int pane, int line, const std::wstring& comment);
        // Sets a line's flags and comment as they were (loading an archive).
        void RestoreLineState(int pane, int line, uint8_t flags, const std::wstring& comment);
        void ClearBookmarks();
        // The next (or previous) bookmarked line, wrapping around; -1 if there is none.
        int FindBookmark(int pane, int line, bool forward) const;
        int BookmarkCount() const;

        // Groups several edits into one undo step (Replace All).
        // pane and caret: where Undo puts the caret back.
        void BeginGroup(int pane = 0, TextPos caret = TextPos());
        void EndGroup();

        // ---- undo ----
        bool CanUndo() const
        {
            return !m_undo.empty();
        }
        bool CanRedo() const
        {
            return !m_redo.empty();
        }
        // Return false when there is nothing to undo; pane and caret receive where the change was.
        bool Undo(int& pane, TextPos& caret);
        bool Redo(int& pane, TextPos& caret);

        // Increases with every change of text or chunks; views use it to know when to refresh.
        unsigned Revision() const
        {
            return m_revision;
        }

    private:
        struct Step
        {
            int pane = 0;
            LineEdit edit;   // what was applied
            Lines removed;   // what it replaced
        };
        struct Action
        {
            std::vector<Step> steps;
            int pane = 0;
            TextPos caretBefore;
            TextPos caretAfter;
            bool typing = false;
            unsigned id = 0;
        };

        void ApplyEdit(int pane, LineEdit edit);  // adds a step to the open action
        Lines ApplyToBuffer(int pane, const LineEdit& edit);
        void OpenAction(int pane, TextPos caretBefore);
        void CloseAction(TextPos caretAfter, bool typing);
        unsigned CurrentId(int pane) const;
        void Changed();

        int m_paneCount = 2;
        TextBuffer m_buffers[3];
        CompareOptions m_options;
        std::vector<Chunk> m_chunks;
        std::vector<SyncPoint> m_syncPoints;
        std::vector<Action> m_undo;
        std::vector<Action> m_redo;
        Action m_open;
        int m_openDepth = 0;
        unsigned m_nextId = 1;
        unsigned m_savedId[3] = {};
        bool m_metaDirty[3] = {};  // encoding changed since the last save
        unsigned m_revision = 1;
    };
}
