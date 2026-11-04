// BinaryDiff.cpp - offset-aligned or LCS byte comparison.
#include "BinaryDiff.h"

#include <algorithm>

namespace mf
{
    int BinaryDiff::Differences() const
    {
        int count = 0;
        for (const Chunk& chunk : chunks)
            count += chunk.IsDifference();
        return count;
    }

    BinaryDiff DiffBytes(const std::string& a, const std::string& b, size_t diffLimit)
    {
        BinaryDiff result;
        const int na = static_cast<int>(a.size());
        const int nb = static_cast<int>(b.size());
        if (a.size() == b.size() || a.size() + b.size() > diffLimit)
        {
            result.aligned = true;
            const int common = (std::min)(na, nb);
            int i = 0;
            while (i < common)
            {
                Chunk chunk;
                chunk.start[0] = chunk.start[1] = i;
                const bool same = a[static_cast<size_t>(i)] == b[static_cast<size_t>(i)];
                int j = i;
                while (j < common && (a[static_cast<size_t>(j)] == b[static_cast<size_t>(j)]) == same)
                    ++j;
                // Short equal runs inside a changed region are part of the change (easier to read).
                if (!same)
                {
                    int k = j;
                    while (k < common)
                    {
                        int run = k;
                        while (run < common && a[static_cast<size_t>(run)] == b[static_cast<size_t>(run)])
                            ++run;
                        if (run - k >= 8 || run == common)
                            break;
                        while (run < common && a[static_cast<size_t>(run)] != b[static_cast<size_t>(run)])
                            ++run;
                        k = run;
                        j = run;
                    }
                }
                chunk.kind = same ? ChunkKind::Equal : ChunkKind::Change;
                chunk.count[0] = chunk.count[1] = j - i;
                result.chunks.push_back(chunk);
                i = j;
            }
            if (na != nb)
            {
                Chunk tail;
                tail.kind = ChunkKind::Change;
                tail.start[0] = tail.start[1] = common;
                tail.count[0] = na - common;
                tail.count[1] = nb - common;
                if (!result.chunks.empty() && result.chunks.back().kind == ChunkKind::Change)
                {
                    result.chunks.back().count[0] += tail.count[0];
                    result.chunks.back().count[1] += tail.count[1];
                }
                else
                {
                    result.chunks.push_back(tail);
                }
            }
        }
        else
        {
            std::vector<int> keysA(a.begin(), a.end()), keysB(b.begin(), b.end());
            result.chunks = DiffKeys(keysA, keysB);
        }
        for (const Chunk& chunk : result.chunks)
        {
            if (chunk.IsDifference())
                result.differentBytes += static_cast<uint64_t>((std::max)(chunk.count[0], chunk.count[1]));
        }
        return result;
    }
}
