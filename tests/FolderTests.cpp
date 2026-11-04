// FolderTests.cpp - folder comparison on temporary folder trees: patterns, the three compare modes,
// three-way classification, synchronization and automatic folder merging.
#include "TestFramework.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <filesystem>
#include <fstream>

#include "FolderCompare.h"

using namespace mf;
namespace fs = std::filesystem;

namespace
{
    // A fresh folder under %TEMP%, removed when the test ends.
    class TempTree
    {
    public:
        explicit TempTree(const wchar_t* name)
        {
            m_root = fs::temp_directory_path() / (std::wstring(L"mergefiles_test_") + name);
            std::error_code ignored;
            fs::remove_all(m_root, ignored);
            fs::create_directories(m_root);
        }
        ~TempTree()
        {
            std::error_code ignored;
            fs::remove_all(m_root, ignored);
        }
        std::wstring Path(const std::wstring& rel) const
        {
            return (m_root / rel).wstring();
        }
        void Write(const std::wstring& rel, const std::string& content) const
        {
            const fs::path path = m_root / rel;
            fs::create_directories(path.parent_path());
            std::ofstream(path, std::ios::binary) << content;
        }
        std::string Read(const std::wstring& rel) const
        {
            std::ifstream in(m_root / rel, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
        bool Exists(const std::wstring& rel) const
        {
            return fs::exists(m_root / rel);
        }
        // Gives a file the same last-write time as another.
        void CopyTime(const std::wstring& from, const std::wstring& to) const
        {
            fs::last_write_time(m_root / to, fs::last_write_time(m_root / from));
        }

    private:
        fs::path m_root;
    };

    ItemStatus StatusOf(const FolderComparison& comparison, const std::wstring& rel)
    {
        const int item = comparison.FindItem(rel);
        return item < 0 ? ItemStatus::Pending : comparison.Item(item).status;
    }

    std::wstring Name(ItemStatus status)
    {
        return StatusName(status);
    }
}

TEST(Folders, WildcardPatterns)
{
    CHECK(WildcardMatch(L"*.obj", L"main.OBJ"));
    CHECK(WildcardMatch(L"te?t*", L"test_file"));
    CHECK(!WildcardMatch(L"*.obj", L"main.obj.txt"));
    CHECK(MatchesPatternList(L"*.obj; .git\\ ;build\\", L".git", true));
    CHECK(!MatchesPatternList(L"*.obj;.git\\", L".git", false));  // a folder pattern doesn't match files
    CHECK(MatchesPatternList(L"*.obj;.git\\", L"x.obj", false));
}

TEST(Folders, TwoWayStatusesAndFilters)
{
    TempTree tree(L"twoway");
    tree.Write(L"L\\same.txt", "same\n");
    tree.Write(L"R\\same.txt", "same\n");
    tree.Write(L"L\\diff.txt", "left\n");
    tree.Write(L"R\\diff.txt", "right\n");
    tree.Write(L"L\\only_left.txt", "x");
    tree.Write(L"R\\only_right.txt", "y");
    tree.Write(L"L\\sub\\deep.txt", "a");
    tree.Write(L"R\\sub\\deep.txt", "b");
    tree.Write(L"L\\.git\\config", "ignored");
    tree.Write(L"R\\main.obj", "ignored");

    FolderOptions options;
    options.exclude = L".git\\;*.obj";
    FolderComparison comparison({ tree.Path(L"L"), tree.Path(L"R") }, options);
    CHECK(comparison.Run());
    CHECK_EQ(Name(StatusOf(comparison, L"same.txt")), Name(ItemStatus::Identical));
    CHECK_EQ(Name(StatusOf(comparison, L"diff.txt")), Name(ItemStatus::Different));
    CHECK_EQ(Name(StatusOf(comparison, L"only_left.txt")), Name(ItemStatus::LeftOnly));
    CHECK_EQ(Name(StatusOf(comparison, L"only_right.txt")), Name(ItemStatus::RightOnly));
    CHECK_EQ(Name(StatusOf(comparison, L"sub")), Name(ItemStatus::Different));  // something inside differs
    CHECK_EQ(Name(StatusOf(comparison, L"SUB\\DEEP.TXT")), Name(ItemStatus::Different));  // case-insensitive lookup
    CHECK_EQ(comparison.FindItem(L".git"), -1);
    CHECK_EQ(comparison.FindItem(L"main.obj"), -1);
    // Folders sort before files.
    CHECK(comparison.Item(comparison.Item(0).children[0]).isDir);
    const FolderComparison::Counts counts = comparison.CountFiles();
    CHECK_EQ(counts.identical, 1);
    CHECK_EQ(counts.different, 2);
    CHECK_EQ(counts.leftOnly, 1);
    CHECK_EQ(counts.rightOnly, 1);
}

TEST(Folders, CompareModes)
{
    TempTree tree(L"modes");
    tree.Write(L"L\\crlf.txt", "one\r\ntwo\r\n");
    tree.Write(L"R\\crlf.txt", "one\ntwo\n");
    tree.Write(L"L\\samesize.txt", "abc");
    tree.Write(L"R\\samesize.txt", "xyz");
    tree.CopyTime(L"L\\samesize.txt", L"R\\samesize.txt");

    FolderOptions options;
    options.mode = FolderCompareMode::Quick;
    FolderComparison quick({ tree.Path(L"L"), tree.Path(L"R") }, options);
    quick.Run();
    // Same size and time: quick mode can't tell them apart.
    CHECK_EQ(Name(StatusOf(quick, L"samesize.txt")), Name(ItemStatus::Identical));

    options.mode = FolderCompareMode::Binary;
    FolderComparison binary({ tree.Path(L"L"), tree.Path(L"R") }, options);
    binary.Run();
    CHECK_EQ(Name(StatusOf(binary, L"samesize.txt")), Name(ItemStatus::Different));
    CHECK_EQ(Name(StatusOf(binary, L"crlf.txt")), Name(ItemStatus::Different));

    options.mode = FolderCompareMode::Thorough;  // line endings ignored by default
    FolderComparison thorough({ tree.Path(L"L"), tree.Path(L"R") }, options);
    thorough.Run();
    CHECK_EQ(Name(StatusOf(thorough, L"crlf.txt")), Name(ItemStatus::Identical));
    CHECK_EQ(Name(StatusOf(thorough, L"samesize.txt")), Name(ItemStatus::Different));
}

TEST(Folders, ThreeWayClassificationAndMerge)
{
    TempTree tree(L"threeway");
    auto all = [&](const std::wstring& rel, const std::string& left, const std::string& base, const std::string& right) {
        if (!left.empty())
            tree.Write(L"L\\" + rel, left);
        if (!base.empty())
            tree.Write(L"A\\" + rel, base);
        if (!right.empty())
            tree.Write(L"R\\" + rel, right);
    };
    all(L"unchanged.txt", "u\n", "u\n", "u\n");
    all(L"left.txt", "L\n", "base\n", "base\n");
    all(L"right.txt", "base\n", "base\n", "R\n");
    all(L"both.txt", "same\n", "base\n", "same\n");
    all(L"conflict.txt", "x\n", "base\n", "y\n");
    all(L"mergeable.txt", "1\nLEFT\n3\n4\n5\n", "1\n2\n3\n4\n5\n", "1\n2\n3\n4\nRIGHT\n");
    all(L"added_left.txt", "new\n", "", "");
    all(L"dir\\added_right.txt", "", "", "r\n");
    fs::create_directories(tree.Path(L"L\\dir"));
    fs::create_directories(tree.Path(L"A\\dir"));

    FolderOptions options;
    FolderComparison comparison({ tree.Path(L"L"), tree.Path(L"A"), tree.Path(L"R") }, options);
    CHECK(comparison.IsThreeWay());
    comparison.Run();
    CHECK_EQ(Name(StatusOf(comparison, L"unchanged.txt")), Name(ItemStatus::Identical));
    CHECK_EQ(Name(StatusOf(comparison, L"left.txt")), Name(ItemStatus::LeftChanged));
    CHECK_EQ(Name(StatusOf(comparison, L"right.txt")), Name(ItemStatus::RightChanged));
    CHECK_EQ(Name(StatusOf(comparison, L"both.txt")), Name(ItemStatus::BothSame));
    CHECK_EQ(Name(StatusOf(comparison, L"conflict.txt")), Name(ItemStatus::Conflict));
    CHECK_EQ(Name(StatusOf(comparison, L"added_left.txt")), Name(ItemStatus::LeftChanged));
    CHECK_EQ(Name(StatusOf(comparison, L"dir\\added_right.txt")), Name(ItemStatus::RightChanged));
    CHECK_EQ(Name(StatusOf(comparison, L"")), Name(ItemStatus::Conflict));  // the root holds a conflict

    // A dry run changes nothing.
    FolderComparison::MergeReport dry = comparison.AutoMerge(true);
    CHECK_EQ(tree.Read(L"A\\left.txt"), std::string("base\n"));
    CHECK_EQ(dry.applied, 5);
    CHECK_EQ(dry.textMerged, 1);
    CHECK_EQ(dry.conflicts.size(), 1u);

    const FolderComparison::MergeReport report = comparison.AutoMerge(false);
    CHECK(report.errors.empty());
    CHECK_EQ(report.applied, 5);
    CHECK_EQ(report.textMerged, 1);
    CHECK_EQ(report.conflicts.size(), 1u);
    CHECK_EQ(tree.Read(L"A\\left.txt"), std::string("L\n"));
    CHECK_EQ(tree.Read(L"A\\right.txt"), std::string("R\n"));
    CHECK_EQ(tree.Read(L"A\\both.txt"), std::string("same\n"));
    CHECK_EQ(tree.Read(L"A\\added_left.txt"), std::string("new\n"));
    CHECK_EQ(tree.Read(L"A\\dir\\added_right.txt"), std::string("r\n"));
    CHECK_EQ(tree.Read(L"A\\mergeable.txt"), std::string("1\nLEFT\n3\n4\nRIGHT\n"));
    CHECK_EQ(tree.Read(L"A\\conflict.txt"), std::string("base\n"));
    // The comparison was refreshed: merged items now show what is left to do.
    CHECK_EQ(Name(StatusOf(comparison, L"left.txt")), Name(ItemStatus::RightChanged));
    CHECK_EQ(Name(StatusOf(comparison, L"conflict.txt")), Name(ItemStatus::Conflict));
}

TEST(Folders, CopyItemSynchronizes)
{
    TempTree tree(L"sync");
    tree.Write(L"L\\a.txt", "new\n");
    tree.Write(L"R\\a.txt", "old\n");
    tree.Write(L"L\\folder\\x.txt", "x");
    tree.Write(L"L\\folder\\inner\\y.txt", "y");
    fs::create_directories(tree.Path(L"R"));
    SetFileAttributesW(tree.Path(L"R\\a.txt").c_str(), FILE_ATTRIBUTE_READONLY);

    FolderComparison comparison({ tree.Path(L"L"), tree.Path(L"R") }, FolderOptions());
    comparison.Run();
    std::wstring error;
    CHECK(comparison.CopyItem(comparison.FindItem(L"a.txt"), 0, 1, error));
    CHECK_EQ(tree.Read(L"R\\a.txt"), std::string("new\n"));
    CHECK_EQ(Name(StatusOf(comparison, L"a.txt")), Name(ItemStatus::Identical));
    CHECK(comparison.CopyItem(comparison.FindItem(L"folder"), 0, 1, error));
    CHECK(tree.Exists(L"R\\folder\\inner\\y.txt"));
    CHECK_EQ(Name(StatusOf(comparison, L"folder")), Name(ItemStatus::Identical));
    CHECK_EQ(Name(StatusOf(comparison, L"")), Name(ItemStatus::Identical));
}
