// DocumentTests.cpp - Phase 6: DEFLATE, zip and tar reading, archives compared as folders, and the
// text of Word, Excel, PowerPoint, OpenDocument and RTF files (samples from testdata\make_samples.py).
#include "TestFramework.h"

#include <filesystem>

#include "DocumentText.h"
#include "Encoding.h"
#include "FolderCompare.h"
#include "Packed.h"

using namespace mf;

namespace
{
    std::wstring Data(const wchar_t* relative)
    {
        return std::wstring(MERGEFILES_TESTDATA) + L"/" + relative;
    }

    std::wstring DocText(const wchar_t* relative)
    {
        std::wstring text, error;
        if (!ReadDocumentText(Data(relative), text, error))
            return L"ERROR: " + error;
        return text;
    }

    std::string BigText()
    {
        std::string text;
        char line[128];
        for (int i = 1; i <= 5000; ++i)
        {
            sprintf_s(line, "Line %05d: the quick brown fox jumps over the lazy dog %d\n", i, i * 7 % 13);
            text += line;
        }
        return text;
    }
}

TEST(Packed, ZipEntriesDeflateStoredAndUtf8Names)
{
    std::wstring error;
    std::unique_ptr<PackedArchive> zip = PackedArchive::Open(Data(L"archives/mixed.zip"), error);
    CHECK(zip != nullptr);
    if (!zip)
        return;
    std::string bytes;
    CHECK(zip->ReadPath(L"data/big.txt", bytes, error));  // dynamic Huffman, several blocks
    CHECK(bytes == BigText());
    CHECK(zip->ReadPath(L"stored.txt", bytes, error));
    CHECK(bytes == "kept as is\n");
    CHECK(zip->ReadPath(L"fixed.txt", bytes, error));
    CHECK(bytes == "abcabcabcabc\n");
    CHECK(zip->ReadPath(L"CAF\x00C9.TXT", bytes, error));  // UTF-8 name, case-insensitive
    CHECK(bytes == "na\xC3\xAFve\n");
    CHECK_EQ(zip->Children(L"").size(), 4u);  // data, stored.txt, café.txt, fixed.txt
    CHECK_EQ(zip->Children(L"data").size(), 1u);
    CHECK(!zip->ReadPath(L"missing.txt", bytes, error));
}

TEST(Packed, TarLongAndPaxNamesAndGzip)
{
    std::wstring error;
    std::unique_ptr<PackedArchive> tar = PackedArchive::Open(Data(L"archives/mixed.tar"), error);
    CHECK(tar != nullptr);
    if (!tar)
        return;
    std::wstring longName = L"deep\\";
    for (int i = 0; i < 5; ++i)
        longName += L"very_long_folder_name_";
    longName += L"\\file.txt";
    std::string bytes;
    CHECK(tar->ReadPath(longName, bytes, error));
    CHECK(bytes == "long\n");
    CHECK(tar->ReadPath(L"\x00FC" L"ber.txt", bytes, error));
    CHECK(bytes == "umlaut\n");
    const int empty = tar->Find(L"empty");
    CHECK(empty >= 0 && tar->Entries()[static_cast<size_t>(empty)].isDir);

    std::unique_ptr<PackedArchive> gz = PackedArchive::Open(Data(L"archives/right.tar.gz"), error);
    CHECK(gz != nullptr);
    if (gz)
    {
        CHECK(gz->ReadPath(L"src/util.cpp", bytes, error));
        std::string onDisk;
        ReadFileBytes(Data(L"folders/right/src/util.cpp"), onDisk, error);
        CHECK(bytes == onDisk);
    }
    std::string corrupt = "\x1F\x8B\x08\x00\x00\x00\x00\x00\x00\x00garbage-garbage";
    std::string out;
    CHECK(!Gunzip(corrupt, out));
}

TEST(Packed, ArchivesComparedAsFolders)
{
    // left.zip and right.tar.gz hold testdata\folders\left and right: the same results as the folders.
    FolderOptions options;
    options.mode = FolderCompareMode::Binary;
    FolderComparison archives({ Data(L"archives/left.zip"), Data(L"archives/right.tar.gz") }, options);
    CHECK(archives.IsArchiveSide(0) && archives.IsArchiveSide(1));
    archives.Run();
    FolderComparison folders({ Data(L"folders/left"), Data(L"folders/right") }, options);
    folders.Run();
    const FolderComparison::Counts a = archives.CountFiles(), f = folders.CountFiles();
    CHECK_EQ(a.identical, f.identical);
    CHECK_EQ(a.different, f.different);
    CHECK_EQ(a.leftOnly, f.leftOnly);
    CHECK_EQ(a.rightOnly, f.rightOnly);
    CHECK_EQ(a.errors, 0);

    // An archive against the folder it was made from: everything identical.
    FolderComparison mixed({ Data(L"archives/left.zip"), Data(L"folders/left") }, options);
    mixed.Run();
    CHECK(mixed.Item(0).status == ItemStatus::Identical);
    const int item = mixed.FindItem(L"src\\util.cpp");
    std::string bytes;
    std::wstring error;
    CHECK(item > 0 && mixed.ReadItem(item, 0, bytes, error) && !bytes.empty());

    // Archives are read-only; copying out of one extracts the file.
    namespace fs = std::filesystem;
    const fs::path target = fs::temp_directory_path() / L"mergefiles_test_extract";
    std::error_code ignored;
    fs::remove_all(target, ignored);
    fs::create_directories(target);
    FolderComparison extract({ Data(L"archives/left.zip"), target.wstring() }, options);
    extract.Run();
    const int src = extract.FindItem(L"src");
    CHECK(src > 0 && extract.CopyItem(src, 0, 1, error));
    CHECK(fs::exists(target / L"src" / L"util.cpp"));
    CHECK(!extract.CopyItem(src, 1, 0, error));
    CHECK(error.find(L"read-only") != std::wstring::npos);
    fs::remove_all(target, ignored);
}

TEST(Documents, WordText)
{
    CHECK_EQ(DocText(L"documents/report_v1.docx"),
        std::wstring(L"Quarterly report\nSales rose by 12% in the third quarter.\nRegion\tNorth & South\nItem\tCost\nPaper\t40\nClosing remarks.\n"));
    CHECK_EQ(DocText(L"documents/report_v2.docx"),
        std::wstring(L"Quarterly report\nSales rose by 15% in the third quarter.\nRegion\tNorth & South\nItem\tCost\nPaper\t45\nInk\t30\nClosing remarks.\nNext review in May.\n"));
}

TEST(Documents, ExcelAndPowerPointText)
{
    CHECK_EQ(DocText(L"documents/budget_v1.xlsx"), std::wstring(L"[Sheet: Budget]\nItem\tCost\nPaper\t40\n\nTotal\t40\t\tnote\n\n[Sheet: Notes]\nDone\tTRUE\n"));
    CHECK_EQ(DocText(L"documents/slides.pptx"), std::wstring(L"[Slide 1]\nWelcome\nAgenda item\n\n[Slide 2]\nThanks\n"));
}

TEST(Documents, OpenDocumentText)
{
    CHECK_EQ(DocText(L"documents/notes_v1.odt"),
        std::wstring(L"Meeting notes\nAttendees:  Ann, Bob\t(remote)\nDecided to ship on Friday.\nTask\tOwner\nDocs and help\tAnn\n"));
    CHECK_EQ(DocText(L"documents/budget.ods"), std::wstring(L"[Sheet: Budget]\nItem\tCost\nPaper\t\t\t40\n"));
}

TEST(Documents, RtfTextAndErrors)
{
    CHECK_EQ(DocText(L"documents/menu_v1.rtf"), std::wstring(L"Caf\x00E9 menu\nPrice: 5\tEUR\nUnicode: \x20ACuro\nTea\t2\nLast line\n"));
    std::wstring text, error;
    CHECK(!ExtractDocumentText(std::string("\xD0\xCF\x11\xE0 old binary", 14), L"old.docx", text, error));
    CHECK(error.find(L"old binary Office format") != std::wstring::npos);
    CHECK(IsDocumentFileName(L"C:\\x\\Report.DOCX") && IsDocumentFileName(L"a.ods") && !IsDocumentFileName(L"a.txt"));
}

TEST(Documents, ThoroughFolderComparisonUsesText)
{
    FolderOptions options;
    options.mode = FolderCompareMode::Thorough;
    std::wstring note;
    CHECK(FilesAreEqual(Data(L"documents/report_v1.docx"), Data(L"documents/report_v1_resaved.docx"), options, note));
    CHECK(note.find(L"Same document text") != std::wstring::npos);
    CHECK(!FilesAreEqual(Data(L"documents/report_v1.docx"), Data(L"documents/report_v2.docx"), options, note));
    options.mode = FolderCompareMode::Binary;
    CHECK(!FilesAreEqual(Data(L"documents/report_v1.docx"), Data(L"documents/report_v1_resaved.docx"), options, note));
}
