// RulesTests.cpp - Phase 2 comparison rules: blank lines, line and block expressions, numeric
// tolerance, line-pairing rules, synchronization points and bookmarks.
#include "TestFramework.h"

#include "Comparison.h"
#include "InlineDiff.h"

using namespace mf;

namespace
{
    Lines L(const wchar_t* text)
    {
        return SplitLines(text);
    }

    int Differences(const std::vector<Chunk>& chunks)
    {
        int count = 0;
        for (const Chunk& chunk : chunks)
            count += chunk.IsDifference();
        return count;
    }

    int Ignored(const std::vector<Chunk>& chunks)
    {
        int count = 0;
        for (const Chunk& chunk : chunks)
            count += chunk.ignored;
        return count;
    }
}

TEST(Rules, BlankLinesCanBeIgnored)
{
    const Lines a = L(L"a\nb\nc\n");
    const Lines b = L(L"a\n\nb\n   \nc\n");
    CompareOptions options;
    CHECK_EQ(Differences(DiffLines(a, b, options)), 2);
    options.ignoreBlankLines = true;
    const std::vector<Chunk> chunks = DiffLines(a, b, options);
    CHECK_EQ(Differences(chunks), 0);
    CHECK_EQ(Ignored(chunks), 2);
}

TEST(Rules, LineExpressionsIgnoreComments)
{
    const Lines a = L(L"int x = 1; // old note\n// a comment line\nint y = 2;\n");
    const Lines b = L(L"int x = 1; // new note\nint y = 2;\n");
    CompareOptions options;
    CHECK(Differences(DiffLines(a, b, options)) > 0);
    options.lineExpressions = { L"//.*$" };
    options.whitespace = WhitespaceMode::IgnoreChanges;
    const std::vector<Chunk> chunks = DiffLines(a, b, options);
    CHECK_EQ(Differences(chunks), 0);  // the comment-only line is ignored, the trailing comments stripped
    CHECK_EQ(Ignored(chunks), 1);
    CHECK(CheckRegex(L"([unclosed").size() > 0);
    CHECK(CheckRegex(L"\\$Id[^$]*\\$").empty());
}

TEST(Rules, BlockExpressionsIgnoreGeneratedSections)
{
    const Lines a = L(L"keep\n// <generated>\nold 1\nold 2\n// </generated>\ntail\n");
    const Lines b = L(L"keep\n// <generated>\nnew\n// </generated>\ntail\nreal change\n");
    CompareOptions options;
    options.blockExpressions = { { L"<generated>", L"</generated>" } };
    const std::vector<Chunk> chunks = DiffLines(a, b, options);
    CHECK_EQ(Differences(chunks), 1);  // only "real change"
    CHECK_EQ(Ignored(chunks), 1);
}

TEST(Rules, NumericTolerance)
{
    const Lines a = L(L"x = 1.000\ny = 2.5e3\nname = alpha\nz = 10\n");
    const Lines b = L(L"x = 1.004\ny = 2501\nname = beta\nz = 12\n");
    CompareOptions options;
    CHECK_EQ(Differences(DiffLines(a, b, options)), 1);  // one block of four changed lines
    options.absoluteTolerance = 0.01;
    options.relativeTolerance = 0.1;  // percent
    const std::vector<Chunk> chunks = DiffLines(a, b, options);
    // x and y are within tolerance; name and z are not.
    CHECK_EQ(Differences(chunks), 1);
    CHECK_EQ(Ignored(chunks), 1);
    CHECK(EqualWithinTolerance(L"t=-0.5;", L"t=-0.505;", options));
    CHECK(!EqualWithinTolerance(L"v1 = 3", L"v2 = 3", options));  // digits inside words are text
}

TEST(Rules, LinePairingRule)
{
    // Localization files: pair by key, whatever the translated text.
    const std::wstring a0 = L"greeting=Hello";
    const std::wstring a1 = L"farewell=Goodbye";
    const std::wstring b0 = L"farewell=Au revoir";
    const std::wstring b1 = L"greeting=Bonjour";
    const std::wstring b2 = L"thanks=Merci";
    CompareOptions options;
    options.linePairingRule = L"^(\\w+)=";
    const std::vector<std::pair<int, int>> pairs = PairLines({ &a0, &a1 }, { &b0, &b1, &b2 }, options);
    // Pairs keep their order, so only one of the two key matches can be paired.
    CHECK_EQ(pairs.size(), 1u);
    CHECK(pairs[0] == std::make_pair(0, 1) || pairs[0] == std::make_pair(1, 0));
    const std::vector<std::pair<int, int>> ordered = PairLines({ &a0, &a1 }, { &b1, &b0 }, options);
    CHECK_EQ(ordered.size(), 2u);
}

TEST(Rules, SyncPointsForceAlignment)
{
    Comparison comparison;
    comparison.SetText(0, L"A\nB\nC\nD\n", L"l");
    comparison.SetText(1, L"X\nC\nY\nB\n", L"r");
    // Without a link, C pairs with C. Link B (left 1) to B (right 3) instead.
    SyncPoint point;
    point.line[0] = 1;
    point.line[1] = 3;
    comparison.AddSyncPoint(point);
    CHECK_EQ(comparison.SyncPoints().size(), 1u);
    bool bAligned = false;
    for (const Chunk& chunk : comparison.Chunks())
    {
        if (chunk.kind == ChunkKind::Equal && chunk.start[0] == 1 && chunk.start[1] == 3)
            bAligned = true;
    }
    CHECK(bAligned);
    // Editing above the link moves it with its line.
    comparison.ReplaceRange(1, { 0, 0 }, { 0, 0 }, L"new\n");
    CHECK_EQ(comparison.SyncPoints()[0].line[1], 4);
    comparison.ClearSyncPoints();
    CHECK(comparison.SyncPoints().empty());
}

TEST(Rules, Bookmarks)
{
    Comparison comparison;
    comparison.SetText(0, L"a\nb\nc\nd\n", L"l");
    comparison.SetText(1, L"a\n", L"r");
    comparison.ToggleBookmark(0, 1);
    comparison.ToggleBookmark(0, 3);
    CHECK_EQ(comparison.BookmarkCount(), 2);
    CHECK_EQ(comparison.FindBookmark(0, 1, true), 3);
    CHECK_EQ(comparison.FindBookmark(0, 3, true), 1);  // wraps
    CHECK_EQ(comparison.FindBookmark(0, 2, false), 1);
    CHECK(!comparison.IsModified(0));
    // A bookmark stays with its line when text is typed into it.
    comparison.ReplaceRange(0, { 1, 1 }, { 1, 1 }, L"!", true);
    CHECK(comparison.Buffer(0).GetLine(1).flags & LineBookmark);
    comparison.ClearBookmarks();
    CHECK_EQ(comparison.BookmarkCount(), 0);
}
