// MiscTests.cpp - intra-line differences, line pairing, syntax highlighting and search.
#include "TestFramework.h"

#include "InlineDiff.h"
#include "Search.h"
#include "Syntax.h"

using namespace mf;

TEST(Inline, ChangedTokensAreHighlighted)
{
    const InlineDiff diff = DiffLineChars(L"const size_t nLength(pEngine);", L"const int nLength(pEngine);", CompareOptions());
    CHECK_EQ(diff.a.size(), 1u);
    CHECK_EQ(diff.b.size(), 1u);
    CHECK_EQ(diff.a[0].start, 6);
    CHECK_EQ(diff.a[0].length, 6);  // "size_t"
    CHECK_EQ(diff.b[0].length, 3);  // "int"
    CHECK(diff.similarity > 0.7);
}

TEST(Inline, WhitespaceOptionAppliesWithinLines)
{
    CompareOptions options;
    InlineDiff diff = DiffLineChars(L"a  b", L"a b", options);
    CHECK_EQ(diff.a.size(), 1u);
    options.whitespace = WhitespaceMode::IgnoreChanges;
    diff = DiffLineChars(L"a  b", L"a b", options);
    CHECK(diff.a.empty() && diff.b.empty());
    CHECK_EQ(diff.similarity, 1.0);
}

TEST(Inline, PairLinesMatchesSimilarLines)
{
    const std::wstring a0 = L"const size_t nLength = getLength(side);";
    const std::wstring a1 = L"const unichar* pchData = getLineBegin(side);";
    const std::wstring b0 = L"const int nLength = getLength(side);";
    const std::wstring b1 = L"const wchar_t* pchOtherData = getLineData(1 - side);";
    const std::wstring b2 = L"const wchar_t* pchData = getLineData(side);";
    const std::vector<std::pair<int, int>> pairs = PairLines({ &a0, &a1 }, { &b0, &b1, &b2 }, CompareOptions());
    CHECK_EQ(pairs.size(), 2u);
    CHECK(pairs[0] == std::make_pair(0, 0));
    CHECK(pairs[1] == std::make_pair(1, 2));
}

TEST(Syntax, CppTokens)
{
    std::vector<SyntaxSpan> spans;
    int state = HighlightLine(Language::Cpp, L"int x = 42; // note", 0, spans);
    CHECK_EQ(state, 0);
    CHECK_EQ(spans.size(), 3u);
    CHECK(spans[0].cls == TokenClass::Type);
    CHECK(spans[1].cls == TokenClass::Number);
    CHECK(spans[2].cls == TokenClass::Comment && spans[2].start == 12);

    state = HighlightLine(Language::Cpp, L"return 1; /* begins", 0, spans);
    CHECK(state != 0);
    state = HighlightLine(Language::Cpp, L"still comment */ if", state, spans);
    CHECK_EQ(state, 0);
    CHECK(spans[0].cls == TokenClass::Comment && spans[1].cls == TokenClass::Keyword);

    HighlightLine(Language::Cpp, L"  #include <x.h>", 0, spans);
    CHECK(spans.size() == 1 && spans[0].cls == TokenClass::Preprocessor);
    HighlightLine(Language::Cpp, L"s = \"a \\\" b\";", 0, spans);
    CHECK(spans.size() == 1 && spans[0].length == 8);
}

TEST(Syntax, XmlAndIni)
{
    std::vector<SyntaxSpan> spans;
    int state = HighlightLine(Language::Xml, L"<item name=\"x\" ", 0, spans);
    CHECK(state != 0);
    CHECK(spans[0].cls == TokenClass::Tag && spans[1].cls == TokenClass::Attribute && spans[2].cls == TokenClass::String);
    state = HighlightLine(Language::Xml, L"/> <!-- c -->", state, spans);
    CHECK_EQ(state, 0);
    CHECK(spans.back().cls == TokenClass::Comment);

    HighlightLine(Language::Ini, L"[Section]", 0, spans);
    CHECK(spans.size() == 1 && spans[0].cls == TokenClass::Tag);
    CHECK(LanguageFromPath(L"C:\\x\\Test1.CPP") == Language::Cpp);
    CHECK(LanguageFromPath(L"C:\\x.y\\README") == Language::PlainText);
}

TEST(Search, FindsForwardBackwardAndWraps)
{
    TextBuffer buffer;
    buffer.SetAllText(L"alpha beta\nBeta gamma\nalphabet\n");
    TextPos start, end;
    SearchOptions options;
    CHECK(FindInBuffer(buffer, L"beta", { 0, 0 }, options, start, end));
    CHECK(start == (TextPos{ 0, 6 }));
    CHECK(FindInBuffer(buffer, L"beta", end, options, start, end));
    CHECK(start == (TextPos{ 1, 0 }));
    options.matchCase = true;
    CHECK(FindInBuffer(buffer, L"beta", { 1, 0 }, options, start, end));
    CHECK(start == (TextPos{ 0, 6 }));  // wrapped around
    options.matchCase = false;
    options.wholeWord = true;
    CHECK(FindInBuffer(buffer, L"alpha", { 0, 1 }, options, start, end));
    CHECK(start == (TextPos{ 0, 0 }));  // "alphabet" is not a whole word
    options.wholeWord = false;
    options.backward = true;
    CHECK(FindInBuffer(buffer, L"a", { 1, 0 }, options, start, end));
    CHECK(start == (TextPos{ 0, 9 }));
    options.wrap = false;
    CHECK(!FindInBuffer(buffer, L"zeta", { 0, 0 }, options, start, end));
}
