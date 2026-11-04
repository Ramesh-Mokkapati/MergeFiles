// FolderCompare.h - comparison of two folder hierarchies, or two with a common ancestor: scanning,
// comparing corresponding files (quick, byte-by-byte or thorough), filters, synchronization and
// automatic three-way folder merging.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Diff.h"

namespace mf
{
    enum class FolderCompareMode
    {
        Quick,       // size and last-write time only
        Binary,      // byte by byte
        Thorough,    // text files compared like file comparisons (whitespace, case, line endings, rules)
    };

    struct FolderOptions
    {
        FolderCompareMode mode = FolderCompareMode::Binary;
        // Thorough mode compares files larger than this byte by byte (memory and speed).
        uint64_t thoroughLimit = 16ull * 1024 * 1024;
        // Wildcard patterns separated by ';'. A pattern ending in '\' matches folders only, e.g.
        // ".git\;.vs\;*.obj". Include patterns apply to files only (empty = all files).
        std::wstring include;
        std::wstring exclude;
        CompareOptions text;  // used by thorough mode
        bool useRecycleBin = true;  // deleted and replaced folders go to the Recycle Bin
    };

    // Case-insensitive wildcard match (* and ?).
    bool WildcardMatch(const std::wstring& pattern, const std::wstring& name);
    // True when `name` (a file, or a folder when isDir) matches one of the ';'-separated patterns.
    bool MatchesPatternList(const std::wstring& patterns, const std::wstring& name, bool isDir);

    enum class ItemStatus : unsigned char
    {
        Pending,       // not compared yet
        Identical,     // the same on every side (three-way: unchanged)
        Different,     // two-way: both exist and differ; folders: something inside differs
        LeftOnly,      // two-way: only in the left hierarchy
        RightOnly,     // two-way: only in the right hierarchy
        LeftChanged,   // three-way: only the left side changed it (edited, added or removed)
        RightChanged,  // three-way: only the right side changed it
        BothSame,      // three-way: both sides made the same change
        Conflict,      // three-way: both changed it differently (folders: something inside conflicts)
        Error,         // could not be read
    };

    const wchar_t* StatusName(ItemStatus status);

    struct FolderSide
    {
        bool exists = false;
        bool isDir = false;
        uint64_t size = 0;
        int64_t modified = 0;  // FILETIME ticks (100 ns since 1601, UTC)
        bool readOnly = false;
    };

    struct FolderItem
    {
        std::wstring name;     // file or folder name ("" for the root)
        std::wstring relPath;  // relative to the roots, e.g. L"src\\main.cpp" ("" for the root)
        FolderSide side[3];
        bool isDir = false;    // a folder on every side where it exists
        ItemStatus status = ItemStatus::Pending;
        std::wstring note;     // error text, or why it differs ("folder on one side, file on the other")
        int parent = -1;
        std::vector<int> children;  // sorted: folders first, then by name
    };

    struct FolderProgress
    {
        int scanned = 0;    // items found
        int compared = 0;   // files compared
        std::wstring current;
    };

    class PackedArchive;
    struct FileRef;

    class FolderComparison
    {
    public:
        // Two roots (left, right) or three (left, ancestor, right).
        FolderComparison(std::vector<std::wstring> roots, FolderOptions options);

        int SideCount() const
        {
            return static_cast<int>(m_roots.size());
        }
        bool IsThreeWay() const
        {
            return m_roots.size() == 3;
        }
        const std::wstring& Root(int side) const
        {
            return m_roots[static_cast<size_t>(side)];
        }
        const FolderOptions& Options() const
        {
            return m_options;
        }
        void SetOptions(const FolderOptions& options)
        {
            m_options = options;
        }

        // Scans and compares everything. Returns false when cancelled (results are then partial).
        // progress may be called from this thread at any time (keep it cheap).
        bool Run(const std::atomic<bool>* cancel = nullptr, const std::function<void(const FolderProgress&)>& progress = nullptr);

        const std::vector<FolderItem>& Items() const
        {
            return m_items;
        }
        const FolderItem& Item(int index) const
        {
            return m_items[static_cast<size_t>(index)];
        }
        int FindItem(const std::wstring& relPath) const;
        // For an archive side, the archive's path followed by the path inside it (for display).
        std::wstring FullPath(int item, int side) const;

        // A side whose root is an archive file (.zip, .tar, .tar.gz, ...): compared read-only.
        bool IsArchiveSide(int side) const
        {
            return m_isPack[static_cast<size_t>(side)] != 0;
        }
        // The contents of a file item on one side, from disk or from inside an archive.
        bool ReadItem(int item, int side, std::string& bytes, std::wstring& error) const;

        struct Counts
        {
            int identical = 0, different = 0, leftOnly = 0, rightOnly = 0;
            int leftChanged = 0, rightChanged = 0, bothSame = 0, conflicts = 0, errors = 0;
        };
        Counts CountFiles() const;

        // ---- synchronization (each re-compares the affected items) ----
        // Makes side `to` match side `from` for the item: copies (overwriting) or, when the item is
        // missing on `from`, deletes it from `to` (to the Recycle Bin). Folders are copied whole.
        bool CopyItem(int item, int from, int to, std::wstring& error);
        // Deletes the item on one side (to the Recycle Bin).
        bool DeleteItem(int item, int side, std::wstring& error);

        // Three-way automatic merge into the ancestor (middle) hierarchy: changes made on one side
        // (or the same on both) are applied; files changed differently on both sides are merged
        // line by line when the text merge has no conflicts. Conflicts are left alone.
        struct MergeReport
        {
            int applied = 0;           // copied, added or deleted in the ancestor
            int textMerged = 0;        // conflicting files merged line by line
            std::vector<std::wstring> conflicts;  // relative paths left for manual merging
            std::vector<std::wstring> errors;
        };
        MergeReport AutoMerge(bool dryRun);

        // Re-compares one item (and its subtree) after files changed on disk.
        void RefreshItem(int item);

        // Replaces the results with saved ones (an archive); parents must precede their children and
        // children lists are rebuilt from the parent indices.
        void RestoreItems(std::vector<FolderItem> items);

    private:
        void Scan(int item, const std::atomic<bool>* cancel, FolderProgress& progress, const std::function<void(const FolderProgress&)>& report);
        void CompareTree(int item, const std::atomic<bool>* cancel, FolderProgress& progress, const std::function<void(const FolderProgress&)>& report);
        bool FilesEqual(int item, int a, int b, std::wstring& note) const;
        void Classify(int item);
        void Aggregate(int item);
        void UpdateSide(int item, int side);
        void RefreshUp(int item);
        const PackedArchive* Pack(int side) const;
        FileRef Ref(int item, int side) const;

        std::vector<std::wstring> m_roots;
        std::vector<char> m_isPack;
        mutable std::vector<std::shared_ptr<PackedArchive>> m_packs;
        mutable std::vector<std::wstring> m_packErrors;
        FolderOptions m_options;
        std::vector<FolderItem> m_items;
    };

    // Reads two files and compares them like the folder comparison would (exposed for tests).
    bool FilesAreEqual(const std::wstring& a, const std::wstring& b, const FolderOptions& options, std::wstring& note);
}
