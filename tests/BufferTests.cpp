// BufferTests.cpp - text edits, undo/redo, point-and-click and automatic merging, modified state.
#include "TestFramework.h"

#include <filesystem>

#include "Comparison.h"

using namespace mf;

namespace
{
    std::wstring Text(const Comparison& comparison, int pane)
    {
        return comparison.Buffer(pane).GetAllText();
    }
}

TEST(Buffer, ReplaceTextAcrossLines)
{
    Comparison comparison;
    comparison.SetText(0, L"hello world\nsecond line\nthird", L"a");
    TextPos end = comparison.ReplaceRange(0, { 0, 6 }, { 1, 6 }, L"there,\nnew");
    CHECK_EQ(Text(comparison, 0), std::wstring(L"hello there,\nnew line\nthird"));
    CHECK_EQ(end.line, 1);
    CHECK_EQ(end.col, 3);
    CHECK_EQ(comparison.Buffer(0).GetText({ 0, 6 }, { 1, 3 }), std::wstring(L"there,\nnew"));
    CHECK(comparison.Buffer(0).GetLine(1).flags & LineEdited);

    // Deleting everything leaves one empty line.
    comparison.ReplaceRange(0, { 0, 0 }, comparison.Buffer(0).EndPos(), L"");
    CHECK_EQ(comparison.Buffer(0).LineCount(), 1);
    CHECK_EQ(Text(comparison, 0), std::wstring());
}

TEST(Buffer, NewLinesUseTheFileLineEnding)
{
    Comparison comparison;
    comparison.SetText(0, L"a\r\nb\r\n", L"a");
    comparison.ReplaceRange(0, { 0, 1 }, { 0, 1 }, L"\nx");
    CHECK_EQ(Text(comparison, 0), std::wstring(L"a\r\nx\r\nb\r\n"));
}

TEST(Buffer, UndoRedoRestoresExactly)
{
    Comparison comparison;
    comparison.SetText(0, L"one\ntwo\nthree\n", L"a");
    comparison.SetText(1, L"one\n2\nthree\n", L"b");
    const std::wstring original = Text(comparison, 0);
    comparison.ReplaceRange(0, { 1, 0 }, { 2, 0 }, L"");
    comparison.ReplaceRange(0, { 0, 3 }, { 0, 3 }, L"!");
    CHECK_EQ(Text(comparison, 0), std::wstring(L"one!\nthree\n"));
    CHECK(comparison.IsModified(0));
    CHECK(!comparison.IsModified(1));

    int pane = -1;
    TextPos caret;
    CHECK(comparison.Undo(pane, caret));
    CHECK(comparison.Undo(pane, caret));
    CHECK_EQ(pane, 0);
    CHECK_EQ(Text(comparison, 0), original);
    CHECK(!comparison.IsModified(0));
    CHECK(!comparison.CanUndo());
    CHECK(comparison.Redo(pane, caret));
    CHECK(comparison.Redo(pane, caret));
    CHECK_EQ(Text(comparison, 0), std::wstring(L"one!\nthree\n"));
    CHECK(!comparison.CanRedo());
}

TEST(Buffer, TypingIsOneUndoStep)
{
    Comparison comparison;
    comparison.SetText(0, L"x\n", L"a");
    TextPos caret = { 0, 1 };
    for (const wchar_t* ch : { L"a", L"b", L"c" })
        caret = comparison.ReplaceRange(0, caret, caret, ch, true);
    CHECK_EQ(Text(comparison, 0), std::wstring(L"xabc\n"));
    int pane = 0;
    CHECK(comparison.Undo(pane, caret));
    CHECK_EQ(Text(comparison, 0), std::wstring(L"x\n"));
    CHECK_EQ(caret.col, 1);
    CHECK(!comparison.CanUndo());
}

TEST(Buffer, CopyChunkMergesAndUndoes)
{
    Comparison comparison;
    comparison.SetText(0, L"a\nb\nc\nd\n", L"left");
    comparison.SetText(1, L"a\nB\nc\n", L"right");
    // Chunks: a | b->B | c | -d | (final empty line)
    int changed = -1, removed = -1;
    for (int i = 0; i < static_cast<int>(comparison.Chunks().size()); ++i)
    {
        const Chunk& chunk = comparison.Chunks()[static_cast<size_t>(i)];
        if (chunk.IsDifference() && chunk.count[1] == 1)
            changed = i;
        if (chunk.IsDifference() && chunk.count[1] == 0)
            removed = i;
    }
    CHECK(changed >= 0 && removed >= 0);
    const ComparisonStats stats = comparison.Stats();
    CHECK_EQ(stats.changes, 1);
    CHECK_EQ(stats.removals, 1);

    CHECK(comparison.CopyChunk(removed, 0, 1));   // copy "d" to the right
    CHECK_EQ(Text(comparison, 1), std::wstring(L"a\nB\nc\nd\n"));
    CHECK(comparison.CopyChunk(comparison.ChunkAt(1, 1), 1, 0));  // B back to the left
    CHECK_EQ(Text(comparison, 0), std::wstring(L"a\nB\nc\nd\n"));
    CHECK_EQ(comparison.Chunks().size(), 1u);
    CHECK_EQ(comparison.Stats().Differences(), 0);

    int pane = 0;
    TextPos caret;
    comparison.Undo(pane, caret);
    comparison.Undo(pane, caret);
    CHECK_EQ(Text(comparison, 0), std::wstring(L"a\nb\nc\nd\n"));
    CHECK_EQ(Text(comparison, 1), std::wstring(L"a\nB\nc\n"));
}

TEST(Buffer, AutoMergeIsUndoable)
{
    Comparison comparison(3);
    comparison.SetText(0, L"1\nL\n3\n4\n5\n", L"left");
    comparison.SetText(1, L"1\n2\n3\n4\n5\n", L"ancestor");
    comparison.SetText(2, L"1\n2\n3\n4\nR\n", L"right");
    CHECK(comparison.IsThreeWay());
    int conflicts = -1;
    CHECK_EQ(comparison.AutoMerge(conflicts), 2);
    CHECK_EQ(conflicts, 0);
    CHECK_EQ(Text(comparison, 1), std::wstring(L"1\nL\n3\n4\nR\n"));
    CHECK(comparison.Buffer(1).GetLine(1).flags & LineResolved);
    CHECK(comparison.IsModified(1));
    int pane = 0;
    TextPos caret;
    CHECK(comparison.Undo(pane, caret));
    CHECK_EQ(Text(comparison, 1), std::wstring(L"1\n2\n3\n4\n5\n"));
}

TEST(Buffer, IndentAndOutdent)
{
    Comparison comparison;
    comparison.SetText(0, L"a\n\n\tb\n  c\n", L"x");
    comparison.Indent(0, 0, 3, false, 4, false);
    CHECK_EQ(Text(comparison, 0), std::wstring(L"    a\n\n    \tb\n      c\n"));
    comparison.Indent(0, 0, 3, true, 4, false);
    comparison.Indent(0, 0, 3, true, 4, false);
    CHECK_EQ(Text(comparison, 0), std::wstring(L"a\n\nb\nc\n"));
}

TEST(Buffer, LayoutChangeKeepsFiles)
{
    Comparison comparison;
    comparison.SetText(0, L"left\n", L"l");
    comparison.SetText(1, L"right\n", L"r");
    comparison.SetPaneCount(3);
    CHECK_EQ(Text(comparison, 0), std::wstring(L"left\n"));
    CHECK_EQ(Text(comparison, 1), std::wstring());
    CHECK_EQ(Text(comparison, 2), std::wstring(L"right\n"));
    comparison.SetPaneCount(2);
    CHECK_EQ(Text(comparison, 1), std::wstring(L"right\n"));
}

TEST(Buffer, MapLineFollowsChunks)
{
    Comparison comparison;
    comparison.SetText(0, L"a\nb\nc\nd\n", L"l");
    comparison.SetText(1, L"a\nx\ny\nz\nw\nc\nd\n", L"r");  // b -> x,y,z,w
    CHECK_EQ(comparison.MapLine(0, 0.0, 1), 0.0);
    CHECK_EQ(comparison.MapLine(0, 1.5, 1), 3.0);
    CHECK_EQ(comparison.MapLine(0, 2.0, 1), 5.0);
    CHECK_EQ(comparison.MapLine(1, 6.0, 0), 3.0);
}

TEST(Buffer, NavigationBetweenDifferences)
{
    Comparison comparison(3);
    comparison.SetText(0, L"1\nL\n3\n4\n5\nX\n7\n", L"left");
    comparison.SetText(1, L"1\n2\n3\n4\n5\n6\n7\n", L"ancestor");
    comparison.SetText(2, L"1\n2\n3\n4\n5\nY\n7\n", L"right");
    const int first = comparison.FindDifference(1, 0, true, false);
    CHECK(first >= 0 && comparison.Chunks()[static_cast<size_t>(first)].kind == ChunkKind::LeftOnly);
    const int conflict = comparison.FindDifference(1, 0, true, true);
    CHECK(conflict > first && comparison.Chunks()[static_cast<size_t>(conflict)].IsConflict());
    CHECK_EQ(comparison.FindDifference(1, 6, true, false), -1);
    CHECK_EQ(comparison.FindDifference(1, 6, false, false), conflict);
    CHECK_EQ(comparison.Stats().conflicts, 1);
}

TEST(Buffer, SaveAndReload)
{
    const std::wstring path = (std::filesystem::temp_directory_path() / L"mergefiles_test_save_utf16.txt").wstring();
    Comparison comparison;
    comparison.SetText(0, L"été\r\nline\r\n", L"x");
    comparison.SetEncoding(0, { EncodingType::Utf16LE, true, 0 });
    std::wstring error;
    CHECK(comparison.SaveFile(0, path, error));
    CHECK(!comparison.IsModified(0));
    Comparison reloaded;
    CHECK(reloaded.LoadFile(0, path, error));
    CHECK(reloaded.Buffer(0).encoding == (TextEncoding{ EncodingType::Utf16LE, true, 0 }));
    CHECK_EQ(Text(reloaded, 0), std::wstring(L"été\r\nline\r\n"));
    std::filesystem::remove(path);
}
