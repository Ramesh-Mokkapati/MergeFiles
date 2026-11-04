// Diff.cpp - Myers' O(ND) difference algorithm with the linear-space "middle snake" refinement
// (E. W. Myers, "An O(ND) Difference Algorithm and Its Variations", 1986).
#include "Diff.h"

#include <algorithm>

namespace mf
{
    namespace
    {
        class Myers
        {
        public:
            Myers(const std::vector<int>& a, const std::vector<int>& b, std::vector<char>& changedA, std::vector<char>& changedB)
                : m_a(a.data())
                , m_b(b.data())
                , m_changedA(changedA.data())
                , m_changedB(changedB.data())
            {
            }

            void Compare(int a0, int a1, int b0, int b1)
            {
                while (a0 < a1 && b0 < b1 && m_a[a0] == m_b[b0])
                {
                    ++a0;
                    ++b0;
                }
                while (a0 < a1 && b0 < b1 && m_a[a1 - 1] == m_b[b1 - 1])
                {
                    --a1;
                    --b1;
                }
                if (a0 == a1 || b0 == b1)
                {
                    MarkAll(a0, a1, b0, b1);
                    return;
                }
                int x = 0, y = 0;
                if (!Bisect(a0, a1, b0, b1, x, y))
                {
                    MarkAll(a0, a1, b0, b1);
                    return;
                }
                Compare(a0, a0 + x, b0, b0 + y);
                Compare(a0 + x, a1, b0 + y, b1);
            }

        private:
            void MarkAll(int a0, int a1, int b0, int b1)
            {
                for (int i = a0; i < a1; ++i)
                    m_changedA[i] = 1;
                for (int j = b0; j < b1; ++j)
                    m_changedB[j] = 1;
            }

            // Finds a point (x, y), relative to (a0, b0), on an optimal (or, past the cost limit, a
            // good) edit path, searching forwards from the start and backwards from the end at once.
            bool Bisect(int a0, int a1, int b0, int b1, int& splitX, int& splitY)
            {
                const int n = a1 - a0;
                const int m = b1 - b0;
                const int maxD = (n + m + 1) / 2;
                const int offset = maxD;
                const int length = 2 * maxD + 2;
                m_v1.assign(static_cast<size_t>(length), -1);
                m_v2.assign(static_cast<size_t>(length), -1);
                m_v1[offset + 1] = 0;
                m_v2[offset + 1] = 0;
                const int delta = n - m;
                const bool front = (delta % 2) != 0;
                int k1start = 0, k1end = 0, k2start = 0, k2end = 0;
                const int* a = m_a + a0;
                const int* b = m_b + b0;
                // Past this many edits the search stops looking for the optimum and splits at the
                // furthest point reached, which keeps very different large files fast.
                const int costLimit = (std::max)(1024, static_cast<int>(std::min<long long>(maxD, 1LL << 30)) / 16);

                for (int d = 0; d < maxD; ++d)
                {
                    for (int k1 = -d + k1start; k1 <= d - k1end; k1 += 2)
                    {
                        const int k1Offset = offset + k1;
                        int x1;
                        if (k1 == -d || (k1 != d && m_v1[k1Offset - 1] < m_v1[k1Offset + 1]))
                            x1 = m_v1[k1Offset + 1];
                        else
                            x1 = m_v1[k1Offset - 1] + 1;
                        int y1 = x1 - k1;
                        while (x1 < n && y1 < m && a[x1] == b[y1])
                        {
                            ++x1;
                            ++y1;
                        }
                        m_v1[k1Offset] = x1;
                        if (x1 > n)
                        {
                            k1end += 2;
                        }
                        else if (y1 > m)
                        {
                            k1start += 2;
                        }
                        else if (front)
                        {
                            const int k2Offset = offset + delta - k1;
                            if (k2Offset >= 0 && k2Offset < length && m_v2[k2Offset] != -1)
                            {
                                const int x2 = n - m_v2[k2Offset];
                                if (x1 >= x2)
                                {
                                    splitX = x1;
                                    splitY = y1;
                                    return true;
                                }
                            }
                        }
                    }

                    for (int k2 = -d + k2start; k2 <= d - k2end; k2 += 2)
                    {
                        const int k2Offset = offset + k2;
                        int x2;
                        if (k2 == -d || (k2 != d && m_v2[k2Offset - 1] < m_v2[k2Offset + 1]))
                            x2 = m_v2[k2Offset + 1];
                        else
                            x2 = m_v2[k2Offset - 1] + 1;
                        int y2 = x2 - k2;
                        while (x2 < n && y2 < m && a[n - x2 - 1] == b[m - y2 - 1])
                        {
                            ++x2;
                            ++y2;
                        }
                        m_v2[k2Offset] = x2;
                        if (x2 > n)
                        {
                            k2end += 2;
                        }
                        else if (y2 > m)
                        {
                            k2start += 2;
                        }
                        else if (!front)
                        {
                            const int k1Offset = offset + delta - k2;
                            if (k1Offset >= 0 && k1Offset < length && m_v1[k1Offset] != -1)
                            {
                                const int x1 = m_v1[k1Offset];
                                const int y1 = offset + x1 - k1Offset;
                                if (x1 >= n - x2)
                                {
                                    splitX = x1;
                                    splitY = y1;
                                    return true;
                                }
                            }
                        }
                    }

                    if (d >= costLimit)
                    {
                        // Split at the forward point that got furthest along both files.
                        int bestX = -1, bestY = -1;
                        for (int k1 = -d + k1start; k1 <= d - k1end; k1 += 2)
                        {
                            const int x1 = m_v1[offset + k1];
                            const int y1 = x1 - k1;
                            if (x1 < 0 || x1 > n || y1 < 0 || y1 > m)
                                continue;
                            if (x1 + y1 > bestX + bestY)
                            {
                                bestX = x1;
                                bestY = y1;
                            }
                        }
                        if (bestX < 0 || bestX + bestY == 0 || (bestX == n && bestY == m))
                            return false;
                        splitX = bestX;
                        splitY = bestY;
                        return true;
                    }
                }
                return false;
            }

            const int* m_a;
            const int* m_b;
            char* m_changedA;
            char* m_changedB;
            std::vector<int> m_v1;
            std::vector<int> m_v2;
        };
    }

    void DiffSequences(const std::vector<int>& a, const std::vector<int>& b, std::vector<char>& changedA, std::vector<char>& changedB)
    {
        changedA.assign(a.size(), 0);
        changedB.assign(b.size(), 0);
        if (a.empty() && b.empty())
            return;
        Myers myers(a, b, changedA, changedB);
        myers.Compare(0, static_cast<int>(a.size()), 0, static_cast<int>(b.size()));
    }

    std::vector<Chunk> DiffKeys(const std::vector<int>& a, const std::vector<int>& b)
    {
        std::vector<char> changedA, changedB;
        DiffSequences(a, b, changedA, changedB);

        std::vector<Chunk> chunks;
        const int na = static_cast<int>(a.size());
        const int nb = static_cast<int>(b.size());
        int i = 0, j = 0;
        while (i < na || j < nb)
        {
            Chunk chunk;
            chunk.start[0] = i;
            chunk.start[1] = j;
            if (i < na && j < nb && !changedA[i] && !changedB[j])
            {
                chunk.kind = ChunkKind::Equal;
                while (i < na && j < nb && !changedA[i] && !changedB[j])
                {
                    ++i;
                    ++j;
                }
            }
            else
            {
                chunk.kind = ChunkKind::Change;
                while (i < na && changedA[i])
                    ++i;
                while (j < nb && changedB[j])
                    ++j;
                if (i == chunk.start[0] && j == chunk.start[1])
                {
                    // Unmatched leftovers (cannot happen with a consistent diff): one final change.
                    i = na;
                    j = nb;
                }
            }
            chunk.count[0] = i - chunk.start[0];
            chunk.count[1] = j - chunk.start[1];
            chunks.push_back(chunk);
        }
        return chunks;
    }
}
