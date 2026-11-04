// DiffTests.cpp - two-way diff: correctness against a brute-force LCS, options and chunk layout.
#include "TestFramework.h"

#include <algorithm>
#include <random>

#include "Diff.h"

using namespace mf;

namespace
{
    int LcsLength(const std::vector<int>& a, const std::vector<int>& b)
    {
        std::vector<std::vector<int>> table(a.size() + 1, std::vector<int>(b.size() + 1, 0));
        for (size_t i = 1; i <= a.size(); ++i)
        {
            for (size_t j = 1; j <= b.size(); ++j)
                table[i][j] = a[i - 1] == b[j - 1] ? table[i - 1][j - 1] + 1 : (std::max)(table[i - 1][j], table[i][j - 1]);
        }
        return table[a.size()][b.size()];
    }

    // Checks the diff is consistent (unchanged elements match in order) and returns their number.
    int CheckConsistent(const std::vector<int>& a, const std::vector<int>& b, const std::vector<char>& changedA, const std::vector<char>& changedB)
    {
        std::vector<int> keptA, keptB;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (!changedA[i])
                keptA.push_back(a[i]);
        }
        for (size_t j = 0; j < b.size(); ++j)
        {
            if (!changedB[j])
                keptB.push_back(b[j]);
        }
        CHECK(keptA == keptB);
        return static_cast<int>(keptA.size());
    }

    // Chunks must cover both files exactly, in order, alternating sensibly.
    void CheckCoverage(const std::vector<Chunk>& chunks, int na, int nb)
    {
        int i = 0, j = 0;
        for (const Chunk& chunk : chunks)
        {
            CHECK_EQ(chunk.start[0], i);
            CHECK_EQ(chunk.start[1], j);
            if (chunk.kind == ChunkKind::Equal)
                CHECK_EQ(chunk.count[0], chunk.count[1]);
            CHECK(chunk.count[0] + chunk.count[1] > 0);
            i += chunk.count[0];
            j += chunk.count[1];
        }
        CHECK_EQ(i, na);
        CHECK_EQ(j, nb);
    }

    Lines L(const wchar_t* text)
    {
        return SplitLines(text);
    }
}

TEST(Diff, RandomSequencesAreMinimal)
{
    std::mt19937 random(12345);
    for (int round = 0; round < 400; ++round)
    {
        const int na = static_cast<int>(random() % 40);
        const int nb = static_cast<int>(random() % 40);
        const int alphabet = 2 + static_cast<int>(random() % 6);
        std::vector<int> a(static_cast<size_t>(na)), b(static_cast<size_t>(nb));
        for (int& x : a)
            x = static_cast<int>(random() % alphabet);
        for (int& x : b)
            x = static_cast<int>(random() % alphabet);
        std::vector<char> changedA, changedB;
        DiffSequences(a, b, changedA, changedB);
        CHECK_EQ(CheckConsistent(a, b, changedA, changedB), LcsLength(a, b));
        CheckCoverage(DiffKeys(a, b), na, nb);
    }
}

TEST(Diff, LargeDifferentFilesStayConsistent)
{
    // Beyond the cost limit the result need not be minimal, but it must stay a valid diff.
    std::mt19937 random(7);
    std::vector<int> a(20000), b(20000);
    for (int& x : a)
        x = static_cast<int>(random() % 1000);
    for (int& x : b)
        x = static_cast<int>(random() % 1000);
    std::vector<char> changedA, changedB;
    DiffSequences(a, b, changedA, changedB);
    CheckConsistent(a, b, changedA, changedB);
    CheckCoverage(DiffKeys(a, b), 20000, 20000);
}

TEST(Diff, EqualFilesGiveOneEqualChunk)
{
    const std::vector<Chunk> chunks = DiffLines(L(L"a\nb\nc\n"), L(L"a\nb\nc\n"), CompareOptions());
    CHECK_EQ(chunks.size(), 1u);
    CHECK(chunks[0].kind == ChunkKind::Equal);
    CHECK_EQ(chunks[0].count[0], 4);
}

TEST(Diff, InsertRemoveChange)
{
    const Lines a = L(L"one\ntwo\nthree\nfour\nfive");
    const Lines b = L(L"one\ninserted\ntwo\nTHREE\nfour");
    const std::vector<Chunk> chunks = DiffLines(a, b, CompareOptions());
    CheckCoverage(chunks, 5, 5);
    // one | +inserted | two | three->THREE | four | -five
    CHECK_EQ(chunks.size(), 6u);
    CHECK(chunks[1].kind == ChunkKind::Change && chunks[1].count[0] == 0 && chunks[1].count[1] == 1);
    CHECK(chunks[3].kind == ChunkKind::Change && chunks[3].count[0] == 1 && chunks[3].count[1] == 1);
    CHECK(chunks[5].kind == ChunkKind::Change && chunks[5].count[0] == 1 && chunks[5].count[1] == 0);
}

TEST(Diff, WhitespaceAndCaseOptions)
{
    const Lines a = L(L"int  x = 1;\n\tfoo();\nBar");
    const Lines b = L(L"int x = 1;  \n    foo();\nbar");
    CompareOptions options;
    CHECK_EQ(DiffLines(a, b, options).size(), 1u);  // all three lines differ
    options.whitespace = WhitespaceMode::IgnoreChanges;
    std::vector<Chunk> chunks = DiffLines(a, b, options);
    CHECK_EQ(chunks.size(), 2u);  // two equal lines, then Bar/bar
    options.ignoreCase = true;
    chunks = DiffLines(a, b, options);
    CHECK_EQ(chunks.size(), 1u);
    CHECK(chunks[0].kind == ChunkKind::Equal);

    options = CompareOptions();
    options.whitespace = WhitespaceMode::IgnoreAll;
    CHECK(NormalizeLine({ L"a b\tc" }, options) == NormalizeLine({ L"abc" }, options));
    options.whitespace = WhitespaceMode::IgnoreChanges;
    CHECK(NormalizeLine({ L"a b\tc" }, options) != NormalizeLine({ L"abc" }, options));
    CHECK(NormalizeLine({ L"  a   b " }, options) == NormalizeLine({ L"a b" }, options));
}

TEST(Diff, LineEndingsOption)
{
    const Lines a = L(L"a\r\nb\r\n");
    const Lines b = L(L"a\nb\n");
    CompareOptions options;
    CHECK_EQ(DiffLines(a, b, options).size(), 1u);
    options.ignoreEol = false;
    const std::vector<Chunk> chunks = DiffLines(a, b, options);
    CHECK_EQ(chunks.size(), 2u);
    CHECK(chunks[0].kind == ChunkKind::Change && chunks[0].count[0] == 2);
}
