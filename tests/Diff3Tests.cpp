// Diff3Tests.cpp - three-way chunks and automatic merging.
#include "TestFramework.h"

#include "Diff3.h"

using namespace mf;

namespace
{
    Lines L(const wchar_t* text)
    {
        return SplitLines(text);
    }

    void CheckCoverage(const std::vector<Chunk>& chunks, const Lines& left, const Lines& base, const Lines& right)
    {
        int at[3] = {};
        for (const Chunk& chunk : chunks)
        {
            for (int file = 0; file < 3; ++file)
            {
                CHECK_EQ(chunk.start[file], at[file]);
                at[file] += chunk.count[file];
            }
        }
        CHECK_EQ(at[0], static_cast<int>(left.size()));
        CHECK_EQ(at[1], static_cast<int>(base.size()));
        CHECK_EQ(at[2], static_cast<int>(right.size()));
    }

    int CountKind(const std::vector<Chunk>& chunks, ChunkKind kind)
    {
        int count = 0;
        for (const Chunk& chunk : chunks)
            count += chunk.kind == kind;
        return count;
    }
}

TEST(Diff3, NonOverlappingChangesMergeCleanly)
{
    const Lines base = L(L"1\n2\n3\n4\n5\n6\n7\n");
    const Lines left = L(L"1\nTWO\n3\n4\n5\n6\n7\n");
    const Lines right = L(L"1\n2\n3\n4\n5\nSIX\n7\nEIGHT\n");
    const std::vector<Chunk> chunks = Diff3Lines(left, base, right, CompareOptions());
    CheckCoverage(chunks, left, base, right);
    CHECK_EQ(CountKind(chunks, ChunkKind::LeftOnly), 1);
    CHECK_EQ(CountKind(chunks, ChunkKind::RightOnly), 2);
    CHECK_EQ(CountKind(chunks, ChunkKind::Conflict), 0);

    const MergeResult merged = Merge3(left, base, right, chunks, false);
    CHECK_EQ(merged.conflicts, 0);
    CHECK_EQ(merged.merged, 3);
    CHECK_EQ(JoinLines(merged.lines), std::wstring(L"1\nTWO\n3\n4\n5\nSIX\n7\nEIGHT\n"));
    CHECK(merged.lines[1].flags == (LineEdited | LineResolved));
    CHECK(merged.lines[0].flags == 0);
}

TEST(Diff3, SameChangeOnBothSides)
{
    const Lines base = L(L"a\nb\nc\n");
    const Lines left = L(L"a\nB\nc\n");
    const Lines right = L(L"a\nB\nc\n");
    const std::vector<Chunk> chunks = Diff3Lines(left, base, right, CompareOptions());
    CHECK_EQ(CountKind(chunks, ChunkKind::BothSame), 1);
    CHECK_EQ(JoinLines(Merge3(left, base, right, chunks, false).lines), std::wstring(L"a\nB\nc\n"));
}

TEST(Diff3, ConflictsAreFlaggedAndKeepAncestor)
{
    const Lines base = L(L"a\nb\nc\nd\n");
    const Lines left = L(L"a\nLEFT\nc\nd\n");
    const Lines right = L(L"a\nRIGHT\nc\nD\n");
    const std::vector<Chunk> chunks = Diff3Lines(left, base, right, CompareOptions());
    CheckCoverage(chunks, left, base, right);
    CHECK_EQ(CountKind(chunks, ChunkKind::Conflict), 1);
    CHECK_EQ(CountKind(chunks, ChunkKind::RightOnly), 1);

    MergeResult merged = Merge3(left, base, right, chunks, false);
    CHECK_EQ(merged.conflicts, 1);
    CHECK_EQ(JoinLines(merged.lines), std::wstring(L"a\nb\nc\nD\n"));

    merged = Merge3(left, base, right, chunks, true);
    CHECK_EQ(JoinLines(merged.lines), std::wstring(L"a\n<<<<<<< left\nLEFT\n||||||| ancestor\nb\n=======\nRIGHT\n>>>>>>> right\nc\nD\n"));
}

TEST(Diff3, AdjacentChangesConflict)
{
    // Changes that touch in ancestor coordinates are treated as one conflict (as git does).
    const Lines base = L(L"a\nb\nc\n");
    const Lines left = L(L"a\nB\nc\n");
    const Lines right = L(L"a\nb\nC\n");
    const std::vector<Chunk> chunks = Diff3Lines(left, base, right, CompareOptions());
    CheckCoverage(chunks, left, base, right);
    CHECK_EQ(CountKind(chunks, ChunkKind::Conflict), 1);
}

TEST(Diff3, InsertionsAndDeletions)
{
    const Lines base = L(L"a\nb\nc\nd\ne\nf\n");
    const Lines left = L(L"new\na\nb\nc\nd\ne\nf\n");      // insertion at the top
    const Lines right = L(L"a\nb\nc\nd\nf\n");               // e deleted
    const std::vector<Chunk> chunks = Diff3Lines(left, base, right, CompareOptions());
    CheckCoverage(chunks, left, base, right);
    const MergeResult merged = Merge3(left, base, right, chunks, false);
    CHECK_EQ(merged.conflicts, 0);
    CHECK_EQ(JoinLines(merged.lines), std::wstring(L"new\na\nb\nc\nd\nf\n"));
}

TEST(Diff3, MissingFinalNewlineGetsOneInsideTheMerge)
{
    const Lines base = L(L"a\nb\nc");
    const Lines left = L(L"a\nb\nc\nleft end");
    const Lines right = L(L"A\nb\nc");
    const std::vector<Chunk> chunks = Diff3Lines(left, base, right, CompareOptions());
    const MergeResult merged = Merge3(left, base, right, chunks, false);
    CHECK_EQ(merged.conflicts, 0);
    CHECK_EQ(JoinLines(merged.lines), std::wstring(L"A\nb\nc\nleft end"));
}
