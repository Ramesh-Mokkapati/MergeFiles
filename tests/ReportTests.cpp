// ReportTests.cpp - Phase 5: bookmark comments, reports, the XML reader and comparison archives.
#include "TestFramework.h"

#include <filesystem>
#include <fstream>

#include "Archive.h"
#include "Report.h"
#include "Xml.h"

using namespace mf;

namespace
{
    bool Contains(const std::wstring& text, const wchar_t* part)
    {
        return text.find(part) != std::wstring::npos;
    }

    size_t Count(const std::wstring& text, const wchar_t* part)
    {
        size_t count = 0;
        for (size_t at = text.find(part); at != std::wstring::npos; at = text.find(part, at + 1))
            ++count;
        return count;
    }

    void MakeSample(Comparison& comparison)
    {
        std::wstring left, right;
        for (int i = 1; i <= 30; ++i)
        {
            left += L"line " + std::to_wstring(i) + L"\n";
            right += (i == 5 ? L"line 5 modified" : (i == 25 ? L"line 25 <changed> & more" : L"line " + std::to_wstring(i))) + std::wstring(L"\n");
        }
        comparison.SetText(0, left, L"left.txt");
        comparison.SetText(1, right, L"right.txt");
    }
}

TEST(Comments, StayWithTheirLine)
{
    Comparison comparison;
    MakeSample(comparison);
    comparison.SetBookmarkComment(0, 4, L"Why upper case?");
    CHECK(comparison.Buffer(0).GetLine(4).flags & LineBookmark);
    comparison.ReplaceRange(0, { 4, 0 }, { 4, 0 }, L">> ", true);  // typing in the line
    CHECK_EQ(comparison.Buffer(0).GetLine(4).comment, std::wstring(L"Why upper case?"));
    comparison.ReplaceRange(0, { 0, 0 }, { 0, 0 }, L"new first line\n");  // the line moves down
    CHECK_EQ(comparison.Buffer(0).GetLine(5).comment, std::wstring(L"Why upper case?"));
    comparison.ToggleBookmark(0, 5);  // removing the bookmark removes the comment
    CHECK(comparison.Buffer(0).GetLine(5).comment.empty());
}

TEST(Reports, HtmlWithContextCommentsAndSlides)
{
    Comparison comparison;
    MakeSample(comparison);
    comparison.SetBookmarkComment(1, 24, L"Check <this> & that");
    ReportOptions options;
    std::wstring html = MakeReport(comparison, options);
    CHECK(Contains(html, L"<!DOCTYPE html>"));
    CHECK(Contains(html, L"class=\"t chg\""));
    CHECK(Contains(html, L"modified</span>"));
    CHECK(Contains(html, L"&lt;changed&gt; &amp; more"));
    CHECK(Contains(html, L"Check &lt;this&gt; &amp; that"));
    CHECK(!Contains(html, L"unchanged line"));  // the whole files
    options.context = 2;
    html = MakeReport(comparison, options);
    CHECK(Contains(html, L"unchanged lines"));
    CHECK(!Contains(html, L">line 15<"));
    options.format = ReportFormat::HtmlSlideshow;
    html = MakeReport(comparison, options);
    CHECK_EQ(Count(html, L"<tbody class=\"slide\">"), 2u);
    CHECK(Contains(html, L"slideMove"));
}

TEST(Reports, XmlAndUnifiedDiff)
{
    Comparison comparison;
    MakeSample(comparison);
    comparison.SetBookmarkComment(0, 0, L"first");
    const std::wstring xml = MakeReport(comparison, { ReportFormat::Xml, -1, L"" });
    XmlNode root;
    std::wstring error;
    CHECK(ParseXml(xml, root, error));
    CHECK_EQ(root.name, std::wstring(L"comparison"));
    int changes = 0;
    for (const XmlNode& node : root.children)
        changes += node.name == L"block" && node.AttributeOr(L"kind", L"") == L"change" ? 1 : 0;
    CHECK_EQ(changes, 2);
    CHECK(root.Child(L"bookmark") && root.Child(L"bookmark")->text == L"first");

    const std::wstring diff = UnifiedDiffText(comparison, 1);
    CHECK(Contains(diff, L"--- left.txt\n+++ right.txt\n@@ -4,3 +4,3 @@\n line 4\n-line 5\n+line 5 modified\n line 6\n"));
}

TEST(Xml, ReaderHandlesEntitiesCdataAndErrors)
{
    XmlNode root;
    std::wstring error;
    CHECK(ParseXml(L"<?xml version=\"1.0\"?><!-- c --><a x=\"1 &amp; 2\" y='&#65;'>t&lt;1&gt;<b/><![CDATA[<raw>]]><c>z</c></a>", root, error));
    CHECK_EQ(root.AttributeOr(L"x", L""), std::wstring(L"1 & 2"));
    CHECK_EQ(root.AttributeOr(L"y", L""), std::wstring(L"A"));
    CHECK_EQ(root.text, std::wstring(L"t<1><raw>"));
    CHECK_EQ(root.children.size(), 2u);
    CHECK_EQ(root.Child(L"c")->text, std::wstring(L"z"));
    CHECK(!ParseXml(L"<a><b></a>", root, error));
    std::string bytes;
    CHECK(Base64Decode(Base64Encode(std::string("\x00\xFFhello", 7)), bytes));
    CHECK(bytes == std::string("\x00\xFFhello", 7));
}

TEST(Archives, FileComparisonRoundTrip)
{
    Comparison comparison;
    MakeSample(comparison);
    comparison.SetEncoding(1, { EncodingType::Utf16LE, true, 0 });
    comparison.SetBookmarkComment(0, 2, L"comment with \"quotes\" & <tags>");
    comparison.SetResolved(1, 4, 4, true);
    SyncPoint point;
    point.line[0] = 10;
    point.line[1] = 10;
    comparison.AddSyncPoint(point);
    CompareOptions options;
    options.lineExpressions = { L"//.*$" };
    comparison.SetOptions(options);

    const std::wstring archive = SaveComparisonArchive(comparison);
    CHECK(ArchiveKindOf(archive) == ArchiveKind::Files);
    std::wstring error;
    std::unique_ptr<Comparison> loaded = LoadComparisonArchive(archive, error);
    CHECK(loaded != nullptr);
    if (!loaded)
        return;
    CHECK_EQ(loaded->Buffer(0).GetAllText(), comparison.Buffer(0).GetAllText());
    CHECK_EQ(loaded->Buffer(1).GetAllText(), comparison.Buffer(1).GetAllText());
    CHECK(loaded->Buffer(1).encoding == (TextEncoding{ EncodingType::Utf16LE, true, 0 }));
    CHECK_EQ(loaded->Buffer(0).GetLine(2).comment, std::wstring(L"comment with \"quotes\" & <tags>"));
    CHECK(loaded->Buffer(1).GetLine(4).flags & LineResolved);
    CHECK_EQ(loaded->SyncPoints().size(), 1u);
    CHECK_EQ(loaded->Options().lineExpressions.size(), 1u);
    CHECK_EQ(loaded->Stats().changes, comparison.Stats().changes);
    CHECK(LoadComparisonArchive(L"<other/>", error) == nullptr);
}

TEST(Archives, FolderComparisonRoundTripAndReport)
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / L"mergefiles_test_folder_archive";
    std::error_code ignored;
    fs::remove_all(root, ignored);
    auto write = [&](const wchar_t* rel, const char* text) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
    };
    write(L"L\\same.txt", "same\n");
    write(L"R\\same.txt", "same\n");
    write(L"L\\sub\\changed.txt", "one\ntwo\n");
    write(L"R\\sub\\changed.txt", "one\nTWO\n");
    write(L"L\\only.txt", "x");

    FolderComparison comparison({ (root / L"L").wstring(), (root / L"R").wstring() }, FolderOptions());
    comparison.Run();
    std::wstring error;
    std::unique_ptr<FolderComparison> loaded = LoadFolderArchive(SaveFolderArchive(comparison), error);
    CHECK(loaded != nullptr);
    if (loaded)
    {
        CHECK_EQ(loaded->Root(1), comparison.Root(1));
        const int item = loaded->FindItem(L"sub\\changed.txt");
        CHECK(item > 0 && loaded->Item(item).status == ItemStatus::Different);
        CHECK(loaded->Item(loaded->FindItem(L"only.txt")).status == ItemStatus::LeftOnly);
        CHECK_EQ(loaded->Item(0).children.size(), comparison.Item(0).children.size());
    }

    FolderReportOptions options;
    options.includeFileReports = true;
    const std::wstring html = MakeFolderReport(comparison, options);
    CHECK(Contains(html, L"changed.txt"));
    CHECK(!Contains(html, L">same.txt<"));  // identical files left out
    CHECK(Contains(html, L">TWO</td>"));  // the file report
    options.xml = true;
    XmlNode parsed;
    CHECK(ParseXml(MakeFolderReport(comparison, options), parsed, error));
    fs::remove_all(root, ignored);
}
