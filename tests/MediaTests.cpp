// MediaTests.cpp - Phase 4: binary comparison, image comparison, patch files and XML formatting.
#include "TestFramework.h"

#include <filesystem>

#include "BinaryDiff.h"
#include "ImageDiff.h"
#include "Patch.h"
#include "XmlFormat.h"

using namespace mf;

TEST(Binary, AlignedComparisonOfSameSizeFiles)
{
    std::string a(1000, 'x'), b = a;
    b[10] = 'y';
    b[12] = 'y';        // close to the first change: joined into one block
    b[500] = 'z';
    const BinaryDiff diff = DiffBytes(a, b);
    CHECK(diff.aligned);
    CHECK_EQ(diff.Differences(), 2);
    CHECK_EQ(diff.differentBytes, 4ull);  // bytes 10..12 and 500
    const BinaryDiff same = DiffBytes(a, a);
    CHECK_EQ(same.Differences(), 0);
}

TEST(Binary, InsertionsFoundInSmallFiles)
{
    const std::string a = "HEADER--payload--TRAILER";
    const std::string b = "HEADER--NEWpayload--TRAILER";
    const BinaryDiff diff = DiffBytes(a, b);
    CHECK(!diff.aligned);
    CHECK_EQ(diff.Differences(), 1);
    bool insertion = false;
    for (const Chunk& chunk : diff.chunks)
        insertion = insertion || (chunk.IsDifference() && chunk.count[0] == 0 && chunk.count[1] == 3);
    CHECK(insertion);
    // Over the limit: offset by offset.
    CHECK(DiffBytes(a, b, 10).aligned);
}

TEST(Images, PixelDifferencesToleranceAndRegions)
{
    Image a;
    a.width = 100;
    a.height = 80;
    a.pixels.assign(100 * 80, 0xFF336699);
    Image b = a;
    b.pixels[5 * 100 + 5] = 0xFF33669C;  // a delta of 3 in one channel
    for (int y = 60; y < 70; ++y)
    {
        for (int x = 80; x < 90; ++x)
            b.pixels[static_cast<size_t>(y) * 100 + static_cast<size_t>(x)] = 0xFFFF0000;
    }
    ImageDiff diff = DiffImages(a, b, 0);
    CHECK(diff.sameSize);
    CHECK_EQ(diff.differing, 101ull);
    CHECK_EQ(diff.regions.size(), 2u);
    diff = DiffImages(a, b, 5);
    CHECK_EQ(diff.differing, 100ull);
    CHECK_EQ(diff.regions.size(), 1u);
    CHECK_EQ(diff.regions[0].left, 64);
    CHECK_EQ(diff.regions[0].top, 32);

    // Different sizes: the extra area differs.
    Image wide = a;
    wide.width = 101;
    wide.pixels.assign(101 * 80, 0xFF336699);
    CHECK_EQ(DiffImages(a, wide, 0).differing, 80ull);
    const Image shown = MakeDifferenceImage(a, DiffImages(a, b, 5));
    CHECK_EQ(shown.At(85, 65), 0xFFE0303Cu);
}

TEST(Images, PngRoundTripThroughWic)
{
    Image image;
    image.width = 7;
    image.height = 5;
    for (int i = 0; i < 35; ++i)
        image.pixels.push_back(0xFF000000u | static_cast<uint32_t>(i * 7));
    const std::wstring path = (std::filesystem::temp_directory_path() / L"mergefiles_test.png").wstring();
    std::wstring error;
    CHECK(SaveImagePng(path, image, error));
    Image loaded;
    CHECK(LoadImageFile(path, loaded, error));
    CHECK_EQ(loaded.width, 7);
    CHECK_EQ(loaded.height, 5);
    CHECK_EQ(DiffImages(image, loaded, 0).differing, 0ull);
    CHECK(IsImageFileName(L"C:\\x\\photo.JPG"));
    CHECK(!IsImageFileName(L"C:\\x\\notes.txt"));
    std::filesystem::remove(path);
}

namespace
{
    const wchar_t* kPatch =
        L"diff --git a/src/app.txt b/src/app.txt\n"
        L"--- a/src/app.txt\t2026-01-01\n"
        L"+++ b/src/app.txt\t2026-01-02\n"
        L"@@ -1,4 +1,4 @@ header\n"
        L" one\n"
        L"-two\n"
        L"+TWO\n"
        L" three\n"
        L" four\n"
        L"@@ -8,3 +8,4 @@\n"
        L" eight\n"
        L" nine\n"
        L"+nine and a half\n"
        L" ten\n"
        L"--- /dev/null\n"
        L"+++ b/new.txt\n"
        L"@@ -0,0 +1,2 @@\n"
        L"+brand\n"
        L"+new\n"
        L"\\ No newline at end of file\n";
}

TEST(Patches, ParseUnifiedDiff)
{
    std::wstring error;
    const std::vector<FilePatch> patches = ParsePatch(kPatch, error);
    CHECK(error.empty());
    CHECK_EQ(patches.size(), 2u);
    CHECK_EQ(patches[0].hunks.size(), 2u);
    CHECK_EQ(patches[0].hunks[0].section, std::wstring(L"header"));
    CHECK_EQ(patches[0].hunks[1].Added(), 1);
    CHECK_EQ(PatchTarget(patches[0], 1), std::wstring(L"src\\app.txt"));
    CHECK(patches[1].isNew);
    CHECK(patches[1].hunks[0].lines.back().noNewline);
    CHECK(!ParsePatch(L"just text\n", error).size());
}

TEST(Patches, ApplyWithOffsetSelectionAndReverse)
{
    std::wstring error;
    const std::vector<FilePatch> patches = ParsePatch(kPatch, error);
    // The file gained two lines at the top since the patch was made: hunks apply 2 lines lower.
    const std::wstring original = L"new a\r\nnew b\r\none\r\ntwo\r\nthree\r\nfour\r\nfive\r\nsix\r\nseven\r\neight\r\nnine\r\nten\r\n";
    std::vector<HunkResult> results;
    const std::wstring patched = ApplyPatch(original, patches[0], {}, results);
    CHECK(results[0].applied && results[1].applied);
    CHECK_EQ(results[0].offset, 2);
    CHECK_EQ(patched, std::wstring(L"new a\r\nnew b\r\none\r\nTWO\r\nthree\r\nfour\r\nfive\r\nsix\r\nseven\r\neight\r\nnine\r\nnine and a half\r\nten\r\n"));

    // Only the second hunk.
    const std::wstring partial = ApplyPatch(original, patches[0], { false, true }, results);
    CHECK(results[0].skipped);
    CHECK(results[1].applied);
    CHECK(partial.find(L"two") != std::wstring::npos && partial.find(L"nine and a half") != std::wstring::npos);

    // Reversing restores the original.
    CHECK_EQ(ApplyPatch(patched, ReversePatch(patches[0]), {}, results), original);

    // A hunk whose context is gone fails without touching the file.
    const std::wstring unrelated = L"alpha\nbeta\n";
    CHECK_EQ(ApplyPatch(unrelated, patches[0], {}, results), unrelated);
    CHECK(!results[0].applied && !results[1].applied);

    // New file, no final newline.
    CHECK_EQ(ApplyPatch(L"", patches[1], {}, results), std::wstring(L"brand\r\nnew"));
}

TEST(Xml, FormatsDenseXml)
{
    const std::wstring dense = L"<?xml version=\"1.0\"?><root><item id=\"1\"  name='a > b'><value>42</value><empty/></item><!-- note --><item/></root>";
    std::wstring formatted, error;
    CHECK(FormatXml(dense, 2, Eol::Lf, formatted, error));
    CHECK_EQ(formatted, std::wstring(L"<?xml version=\"1.0\"?>\n<root>\n  <item id=\"1\" name='a > b'>\n    <value>42</value>\n    <empty/>\n  </item>\n  <!-- note -->\n  <item/>\n</root>\n"));
    CHECK(LooksLikeXml(L"  <a/>"));
    CHECK(!LooksLikeXml(L"int x;"));
    CHECK(!FormatXml(L"<a><!-- open", 2, Eol::Lf, formatted, error));
}
