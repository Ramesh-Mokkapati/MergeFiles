// Diff3.cpp - combines the ancestor->left and ancestor->right differences into three-way chunks.
#include "Diff3.h"

#include <algorithm>

namespace mf
{
    namespace
    {
        // One change of a side relative to the ancestor: base lines [b0, b1) became side lines [s0, s1).
        struct Hunk
        {
            int b0, b1, s0, s1;
            int side;  // 0 = left, 1 = right
        };

        void CollectHunks(const std::vector<Chunk>& chunks, int side, std::vector<Hunk>& hunks)
        {
            for (const Chunk& chunk : chunks)
            {
                if (chunk.kind == ChunkKind::Equal)
                    continue;
                hunks.push_back({ chunk.start[0], chunk.End(0), chunk.start[1], chunk.End(1), side });
            }
        }

        bool SameKeys(const std::vector<int>& a, int a0, int a1, const std::vector<int>& b, int b0, int b1)
        {
            if (a1 - a0 != b1 - b0)
                return false;
            return std::equal(a.begin() + a0, a.begin() + a1, b.begin() + b0);
        }
    }

    std::vector<Chunk> Diff3Keys(const std::vector<int>& left, const std::vector<int>& base, const std::vector<int>& right)
    {
        std::vector<Hunk> hunks;
        CollectHunks(DiffKeys(base, left), 0, hunks);
        CollectHunks(DiffKeys(base, right), 1, hunks);
        std::stable_sort(hunks.begin(), hunks.end(), [](const Hunk& x, const Hunk& y) { return x.b0 < y.b0; });

        std::vector<Chunk> chunks;
        const int baseCount = static_cast<int>(base.size());
        int offset[2] = {};  // side line = base line + offset, outside hunks
        int baseDone = 0;
        size_t i = 0;
        while (i < hunks.size())
        {
            // Group hunks that overlap or touch in ancestor coordinates.
            int b0 = hunks[i].b0;
            int b1 = hunks[i].b1;
            size_t j = i + 1;
            while (j < hunks.size() && hunks[j].b0 <= b1)
            {
                b1 = (std::max)(b1, hunks[j].b1);
                ++j;
            }

            if (b0 > baseDone)
            {
                Chunk equal;
                equal.kind = ChunkKind::Equal;
                equal.start[0] = baseDone + offset[0];
                equal.start[1] = baseDone;
                equal.start[2] = baseDone + offset[1];
                equal.count[0] = equal.count[1] = equal.count[2] = b0 - baseDone;
                chunks.push_back(equal);
            }

            Chunk chunk;
            chunk.start[1] = b0;
            chunk.count[1] = b1 - b0;
            bool has[2] = {};
            for (int side = 0; side < 2; ++side)
            {
                const Hunk* first = nullptr;
                const Hunk* last = nullptr;
                for (size_t k = i; k < j; ++k)
                {
                    if (hunks[k].side != side)
                        continue;
                    if (!first)
                        first = &hunks[k];
                    last = &hunks[k];
                }
                const int file = side == 0 ? 0 : 2;
                int s0, s1;
                if (first)
                {
                    has[side] = true;
                    s0 = first->s0 - (first->b0 - b0);
                    s1 = last->s1 + (b1 - last->b1);
                }
                else
                {
                    s0 = b0 + offset[side];
                    s1 = b1 + offset[side];
                }
                chunk.start[file] = s0;
                chunk.count[file] = s1 - s0;
                offset[side] = s1 - b1;
            }

            if (has[0] && has[1])
                chunk.kind = SameKeys(left, chunk.start[0], chunk.End(0), right, chunk.start[2], chunk.End(2)) ? ChunkKind::BothSame : ChunkKind::Conflict;
            else
                chunk.kind = has[0] ? ChunkKind::LeftOnly : ChunkKind::RightOnly;
            chunks.push_back(chunk);
            baseDone = b1;
            i = j;
        }

        if (baseDone < baseCount || chunks.empty())
        {
            Chunk equal;
            equal.kind = ChunkKind::Equal;
            equal.start[0] = baseDone + offset[0];
            equal.start[1] = baseDone;
            equal.start[2] = baseDone + offset[1];
            equal.count[0] = equal.count[1] = equal.count[2] = baseCount - baseDone;
            if (equal.count[1] > 0)
                chunks.push_back(equal);
        }
        return chunks;
    }

    MergeResult Merge3(const Lines& left, const Lines& base, const Lines& right, const std::vector<Chunk>& chunks, bool conflictMarkers)
    {
        MergeResult result;
        const Eol eol = DominantEol(base, DominantEol(left));
        auto append = [&](const Lines& from, int start, int count, uint8_t flags) {
            for (int k = start; k < start + count; ++k)
            {
                Line line = from[static_cast<size_t>(k)];
                line.flags = static_cast<uint8_t>(line.flags | flags);
                result.lines.push_back(std::move(line));
            }
        };
        auto marker = [&](const wchar_t* text) {
            Line line;
            line.text = text;
            line.eol = eol;
            line.flags = LineEdited;
            result.lines.push_back(std::move(line));
        };

        const uint8_t mergedFlags = LineEdited | LineResolved;
        for (const Chunk& chunk : chunks)
        {
            switch (chunk.kind)
            {
            case ChunkKind::Equal:
                append(base, chunk.start[1], chunk.count[1], 0);
                break;
            case ChunkKind::LeftOnly:
            case ChunkKind::BothSame:
                append(left, chunk.start[0], chunk.count[0], mergedFlags);
                ++result.merged;
                break;
            case ChunkKind::RightOnly:
                append(right, chunk.start[2], chunk.count[2], mergedFlags);
                ++result.merged;
                break;
            default:
                if (chunk.ignored)
                {
                    append(base, chunk.start[1], chunk.count[1], 0);
                    break;
                }
                ++result.conflicts;
                if (!conflictMarkers)
                {
                    append(base, chunk.start[1], chunk.count[1], 0);
                    break;
                }
                marker(L"<<<<<<< left");
                append(left, chunk.start[0], chunk.count[0], 0);
                marker(L"||||||| ancestor");
                append(base, chunk.start[1], chunk.count[1], 0);
                marker(L"=======");
                append(right, chunk.start[2], chunk.count[2], 0);
                marker(L">>>>>>> right");
                break;
            }
        }
        // Only the merge's own last line may lack a line ending; every earlier line needs one.
        for (size_t k = 0; k + 1 < result.lines.size(); ++k)
        {
            if (result.lines[k].eol == Eol::None)
                result.lines[k].eol = eol;
        }
        if (result.lines.empty() || result.lines.back().eol != Eol::None)
            result.lines.push_back(Line());
        return result;
    }
}
