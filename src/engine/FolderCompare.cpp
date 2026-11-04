// FolderCompare.cpp - folder hierarchy comparison through the Win32 file APIs.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include "FolderCompare.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <map>

#include "Diff3.h"
#include "DocumentText.h"
#include "Encoding.h"
#include "Packed.h"

namespace mf
{
    namespace
    {
        const wchar_t* const kMismatch = L"A folder on one side and a file on another";
        const wchar_t* const kIgnoredOnly = L"Identical apart from ignored differences";
        const int64_t kTimeTolerance = 2 * 10000000LL;  // 2 s: FAT and some network shares round times

        // Paths longer than MAX_PATH need the \\?\ prefix.
        std::wstring LongPath(const std::wstring& path)
        {
            if (path.size() < 240 || path.compare(0, 4, L"\\\\?\\") == 0)
                return path;
            if (path.compare(0, 2, L"\\\\") == 0)
                return L"\\\\?\\UNC\\" + path.substr(2);
            return L"\\\\?\\" + path;
        }

        std::wstring Lower(std::wstring text)
        {
            // CharLowerBuff handles every script (towlower in the C locale only knows ASCII).
            if (!text.empty())
                CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
            return text;
        }

        std::wstring Join(const std::wstring& folder, const std::wstring& name)
        {
            if (name.empty())
                return folder;
            if (!folder.empty() && (folder.back() == L'\\' || folder.back() == L'/'))
                return folder + name;
            return folder + L"\\" + name;
        }

        int64_t Ticks(const FILETIME& time)
        {
            return static_cast<int64_t>((static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime);
        }

        struct Entry
        {
            std::wstring name;
            FolderSide info;
            bool reparse = false;
        };

        std::vector<Entry> ListFolder(const std::wstring& folder, std::wstring& error)
        {
            std::vector<Entry> entries;
            WIN32_FIND_DATAW data = {};
            HANDLE find = FindFirstFileExW(LongPath(Join(folder, L"*")).c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
            if (find == INVALID_HANDLE_VALUE)
            {
                const DWORD code = GetLastError();
                if (code != ERROR_FILE_NOT_FOUND)
                    error = LastErrorText(code);
                return entries;
            }
            do
            {
                if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
                    continue;
                Entry entry;
                entry.name = data.cFileName;
                entry.info.exists = true;
                entry.info.isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                entry.info.size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
                entry.info.modified = Ticks(data.ftLastWriteTime);
                entry.info.readOnly = (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
                entry.reparse = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                entries.push_back(std::move(entry));
            } while (FindNextFileW(find, &data));
            FindClose(find);
            return entries;
        }

        FolderSide Stat(const std::wstring& path)
        {
            FolderSide side;
            WIN32_FILE_ATTRIBUTE_DATA data = {};
            if (!GetFileAttributesExW(LongPath(path).c_str(), GetFileExInfoStandard, &data))
                return side;
            side.exists = true;
            side.isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            side.size = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
            side.modified = Ticks(data.ftLastWriteTime);
            side.readOnly = (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
            return side;
        }

    }

    // A file to compare: on disk, or inside an archive (pack != nullptr).
    struct FileRef
    {
        std::wstring path;  // full path (for an archive: archive path + inner path)
        const PackedArchive* pack = nullptr;
        std::wstring inner;  // path inside the archive
    };

    namespace
    {
        bool ReadRef(const FileRef& ref, std::string& bytes, std::wstring& error)
        {
            if (ref.pack)
                return ref.pack->ReadPath(ref.inner, bytes, error);
            return ReadFileBytes(LongPath(ref.path), bytes, error);
        }

        // The entries of a folder inside an archive.
        std::vector<Entry> ListPacked(const PackedArchive& pack, const std::wstring& folder)
        {
            std::vector<Entry> entries;
            for (int index : pack.Children(folder))
            {
                const PackedEntry& packed = pack.Entries()[static_cast<size_t>(index)];
                Entry entry;
                const size_t slash = packed.path.find_last_of(L'\\');
                entry.name = slash == std::wstring::npos ? packed.path : packed.path.substr(slash + 1);
                entry.info.exists = true;
                entry.info.isDir = packed.isDir;
                entry.info.size = packed.size;
                entry.info.modified = packed.modified;
                entry.info.readOnly = true;
                entries.push_back(std::move(entry));
            }
            return entries;
        }

        // 1 = same bytes, 0 = different, -1 = could not read.
        int SameBytes(const FileRef& refA, const FileRef& refB, std::wstring& note)
        {
            if (refA.pack || refB.pack)
            {
                std::string bytesA, bytesB;
                std::wstring error;
                if (!ReadRef(refA, bytesA, error) || !ReadRef(refB, bytesB, error))
                {
                    note = L"Cannot read: " + error;
                    return -1;
                }
                return bytesA == bytesB ? 1 : 0;
            }
            const std::wstring& a = refA.path;
            const std::wstring& b = refB.path;
            const DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
            HANDLE fa = CreateFileW(LongPath(a).c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (fa == INVALID_HANDLE_VALUE)
            {
                note = L"Cannot read " + a + L": " + LastErrorText(GetLastError());
                return -1;
            }
            HANDLE fb = CreateFileW(LongPath(b).c_str(), GENERIC_READ, share, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (fb == INVALID_HANDLE_VALUE)
            {
                note = L"Cannot read " + b + L": " + LastErrorText(GetLastError());
                CloseHandle(fa);
                return -1;
            }
            const DWORD chunk = 256 * 1024;
            std::vector<char> bufferA(chunk), bufferB(chunk);
            int result = 1;
            for (;;)
            {
                DWORD readA = 0, readB = 0;
                if (!ReadFile(fa, bufferA.data(), chunk, &readA, nullptr) || !ReadFile(fb, bufferB.data(), chunk, &readB, nullptr))
                {
                    note = L"Read error: " + LastErrorText(GetLastError());
                    result = -1;
                    break;
                }
                if (readA != readB || memcmp(bufferA.data(), bufferB.data(), readA) != 0)
                {
                    result = 0;
                    break;
                }
                if (readA == 0)
                    break;
            }
            CloseHandle(fa);
            CloseHandle(fb);
            return result;
        }

        // 1 equal, 0 different, -1 error. Sizes and times come from the scan.
        int CompareFiles(const FileRef& a, const FolderSide& sa, const FileRef& b, const FolderSide& sb, const FolderOptions& options, std::wstring& note)
        {
            if (options.mode == FolderCompareMode::Quick)
            {
                const bool same = sa.size == sb.size && std::llabs(sa.modified - sb.modified) <= kTimeTolerance;
                if (!same)
                    note = sa.size != sb.size ? L"Sizes differ" : L"Modification times differ";
                return same ? 1 : 0;
            }
            const bool thorough = options.mode == FolderCompareMode::Thorough && sa.size <= options.thoroughLimit && sb.size <= options.thoroughLimit;
            if (!thorough)
            {
                if (sa.size != sb.size)
                {
                    note = L"Sizes differ";
                    return 0;
                }
                return SameBytes(a, b, note);
            }
            if (sa.size == sb.size)
            {
                const int same = SameBytes(a, b, note);
                if (same != 0)
                    return same;
            }
            // Thorough: compare as text with the file comparison options.
            std::string bytesA, bytesB;
            std::wstring error;
            if (!ReadRef(a, bytesA, error) || !ReadRef(b, bytesB, error))
            {
                note = L"Cannot read: " + error;
                return -1;
            }
            // Word, Excel, OpenDocument and RTF files: compared by their text.
            if (IsDocumentFileName(a.path) && IsDocumentFileName(b.path))
            {
                std::wstring textA, textB;
                if (ExtractDocumentText(bytesA, a.path, textA, error) && ExtractDocumentText(bytesB, b.path, textB, error))
                {
                    const Lines docA = SplitLines(textA);
                    const Lines docB = SplitLines(textB);
                    for (const Chunk& chunk : CompareLines({ &docA, &docB }, options.text))
                    {
                        if (chunk.IsDifference())
                        {
                            note = L"Document text differs";
                            return 0;
                        }
                    }
                    note = L"Same document text (the files differ)";
                    return 1;
                }
            }
            if (LooksBinary(bytesA) || LooksBinary(bytesB))
            {
                note = L"Binary files differ";
                return 0;
            }
            const Lines linesA = SplitLines(DecodeText(bytesA).text);
            const Lines linesB = SplitLines(DecodeText(bytesB).text);
            for (const Chunk& chunk : CompareLines({ &linesA, &linesB }, options.text))
            {
                if (chunk.IsDifference())
                {
                    note = L"Text differs";
                    return 0;
                }
            }
            note = kIgnoredOnly;
            return 1;
        }

        bool DeleteToRecycleBin(const std::wstring& path, bool recycle, std::wstring& error)
        {
            if (!recycle)
            {
                std::error_code code;
                std::filesystem::remove_all(LongPath(path), code);
                if (code)
                    error = L"Cannot delete " + path;
                return !code;
            }
            std::wstring from = path;
            from.push_back(L'\0');  // double-null-terminated list
            SHFILEOPSTRUCTW operation = {};
            operation.wFunc = FO_DELETE;
            operation.pFrom = from.c_str();
            operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
            const int result = SHFileOperationW(&operation);
            if (result != 0 || operation.fAnyOperationsAborted)
            {
                error = L"Cannot delete " + path + L" (error " + std::to_wstring(result) + L")";
                return false;
            }
            return true;
        }

        bool CreateFolders(const std::wstring& path, std::wstring& error)
        {
            if (Stat(path).exists)
                return true;
            const size_t slash = path.find_last_of(L"\\/");
            if (slash != std::wstring::npos && slash > 2 && !CreateFolders(path.substr(0, slash), error))
                return false;
            if (!CreateDirectoryW(LongPath(path).c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            {
                error = L"Cannot create " + path + L": " + LastErrorText(GetLastError());
                return false;
            }
            return true;
        }
    }

    bool WildcardMatch(const std::wstring& pattern, const std::wstring& name)
    {
        // Iterative matcher with backtracking to the last '*'.
        size_t p = 0, n = 0, star = std::wstring::npos, mark = 0;
        while (n < name.size())
        {
            if (p < pattern.size() && (pattern[p] == L'?' || std::towlower(pattern[p]) == std::towlower(name[n])))
            {
                ++p;
                ++n;
            }
            else if (p < pattern.size() && pattern[p] == L'*')
            {
                star = p++;
                mark = n;
            }
            else if (star != std::wstring::npos)
            {
                p = star + 1;
                n = ++mark;
            }
            else
            {
                return false;
            }
        }
        while (p < pattern.size() && pattern[p] == L'*')
            ++p;
        return p == pattern.size();
    }

    bool MatchesPatternList(const std::wstring& patterns, const std::wstring& name, bool isDir)
    {
        size_t start = 0;
        while (start <= patterns.size())
        {
            size_t end = patterns.find(L';', start);
            if (end == std::wstring::npos)
                end = patterns.size();
            std::wstring pattern = patterns.substr(start, end - start);
            while (!pattern.empty() && pattern.front() == L' ')
                pattern.erase(0, 1);
            while (!pattern.empty() && pattern.back() == L' ')
                pattern.pop_back();
            start = end + 1;
            if (pattern.empty())
                continue;
            const bool folderPattern = pattern.back() == L'\\' || pattern.back() == L'/';
            if (folderPattern)
                pattern.pop_back();
            if (folderPattern != isDir)
                continue;
            if (WildcardMatch(pattern, name))
                return true;
        }
        return false;
    }

    const wchar_t* StatusName(ItemStatus status)
    {
        switch (status)
        {
        case ItemStatus::Identical:
            return L"Identical";
        case ItemStatus::Different:
            return L"Different";
        case ItemStatus::LeftOnly:
            return L"Left only";
        case ItemStatus::RightOnly:
            return L"Right only";
        case ItemStatus::LeftChanged:
            return L"Changed on left";
        case ItemStatus::RightChanged:
            return L"Changed on right";
        case ItemStatus::BothSame:
            return L"Same change on both";
        case ItemStatus::Conflict:
            return L"Conflict";
        case ItemStatus::Error:
            return L"Error";
        default:
            return L"Pending";
        }
    }

    bool FilesAreEqual(const std::wstring& a, const std::wstring& b, const FolderOptions& options, std::wstring& note)
    {
        FileRef refA, refB;
        refA.path = a;
        refB.path = b;
        return CompareFiles(refA, Stat(a), refB, Stat(b), options, note) == 1;
    }

    FolderComparison::FolderComparison(std::vector<std::wstring> roots, FolderOptions options) : m_roots(std::move(roots)), m_options(std::move(options))
    {
        for (std::wstring& root : m_roots)
        {
            while (root.size() > 3 && (root.back() == L'\\' || root.back() == L'/'))
                root.pop_back();
            const FolderSide info = Stat(root);
            m_isPack.push_back(info.exists && !info.isDir && IsPackedArchiveName(root) ? 1 : 0);
        }
        m_packs.resize(m_roots.size());
        m_packErrors.resize(m_roots.size());
    }

    const PackedArchive* FolderComparison::Pack(int side) const
    {
        const size_t s = static_cast<size_t>(side);
        if (!m_isPack[s])
            return nullptr;
        if (!m_packs[s] && m_packErrors[s].empty())
        {
            std::wstring error;
            m_packs[s] = PackedArchive::Open(m_roots[s], error);
            if (!m_packs[s])
                m_packErrors[s] = error.empty() ? L"Cannot open " + m_roots[s] : error;
        }
        return m_packs[s].get();
    }

    FileRef FolderComparison::Ref(int item, int side) const
    {
        FileRef ref;
        ref.path = FullPath(item, side);
        ref.pack = Pack(side);
        ref.inner = m_items[static_cast<size_t>(item)].relPath;
        return ref;
    }

    bool FolderComparison::ReadItem(int item, int side, std::string& bytes, std::wstring& error) const
    {
        if (IsArchiveSide(side) && !Pack(side))
        {
            error = m_packErrors[static_cast<size_t>(side)];
            return false;
        }
        return ReadRef(Ref(item, side), bytes, error);
    }

    std::wstring FolderComparison::FullPath(int item, int side) const
    {
        return Join(m_roots[static_cast<size_t>(side)], m_items[static_cast<size_t>(item)].relPath);
    }

    int FolderComparison::FindItem(const std::wstring& relPath) const
    {
        const std::wstring wanted = Lower(relPath);
        for (size_t i = 0; i < m_items.size(); ++i)
        {
            if (m_items[i].parent != -2 && Lower(m_items[i].relPath) == wanted)
                return static_cast<int>(i);
        }
        return -1;
    }

    bool FolderComparison::Run(const std::atomic<bool>* cancel, const std::function<void(const FolderProgress&)>& progress)
    {
        m_items.clear();
        FolderItem root;
        root.isDir = true;
        for (int side = 0; side < SideCount(); ++side)
        {
            root.side[side] = Stat(m_roots[static_cast<size_t>(side)]);
            if (IsArchiveSide(side))
            {
                // An archive is the top folder of its side.
                root.side[side].isDir = Pack(side) != nullptr;
                root.side[side].exists = root.side[side].isDir;
                root.side[side].readOnly = true;
                if (!Pack(side))
                    root.note = m_packErrors[static_cast<size_t>(side)];
            }
        }
        m_items.push_back(std::move(root));
        FolderProgress state;
        Scan(0, cancel, state, progress);
        if (!(cancel && *cancel))
            CompareTree(0, cancel, state, progress);
        if (!(cancel && *cancel))
            return true;
        // Stopped: what exists on one side only is known from the scan alone.
        if (!IsThreeWay())
        {
            for (FolderItem& item : m_items)
            {
                if (item.status != ItemStatus::Pending || item.parent < 0)
                    continue;
                if (item.side[0].exists != item.side[1].exists)
                    item.status = item.side[0].exists ? ItemStatus::LeftOnly : ItemStatus::RightOnly;
            }
        }
        return false;
    }

    void FolderComparison::Scan(int item, const std::atomic<bool>* cancel, FolderProgress& progress, const std::function<void(const FolderProgress&)>& report)
    {
        // Merge the folder listings of every side by (case-insensitive) name.
        std::map<std::wstring, int> byName;
        std::vector<int> children;
        for (int side = 0; side < SideCount(); ++side)
        {
            const FolderSide& info = m_items[static_cast<size_t>(item)].side[side];
            if (!info.exists || !info.isDir)
                continue;
            std::wstring error;
            const PackedArchive* pack = Pack(side);
            const std::vector<Entry> entries = IsArchiveSide(side) ? (pack ? ListPacked(*pack, m_items[static_cast<size_t>(item)].relPath) : std::vector<Entry>())
                                                                   : ListFolder(FullPath(item, side), error);
            if (!error.empty())
                m_items[static_cast<size_t>(item)].note = error;
            for (const Entry& entry : entries)
            {
                if (MatchesPatternList(m_options.exclude, entry.name, entry.info.isDir))
                    continue;
                if (!entry.info.isDir && !m_options.include.empty() && !MatchesPatternList(m_options.include, entry.name, false))
                    continue;
                const std::wstring key = Lower(entry.name);
                auto found = byName.find(key);
                int child;
                if (found == byName.end())
                {
                    FolderItem created;
                    created.name = entry.name;
                    created.relPath = m_items[static_cast<size_t>(item)].relPath.empty() ? entry.name : m_items[static_cast<size_t>(item)].relPath + L"\\" + entry.name;
                    created.parent = item;
                    m_items.push_back(std::move(created));
                    child = static_cast<int>(m_items.size()) - 1;
                    byName.emplace(key, child);
                    children.push_back(child);
                }
                else
                {
                    child = found->second;
                }
                FolderItem& target = m_items[static_cast<size_t>(child)];
                target.side[side] = entry.info;
                if (entry.reparse && entry.info.isDir)
                    target.note = L"Link to another folder (not followed)";
            }
        }

        // isDir: a folder on every side where it exists; a folder/file mismatch is a difference.
        for (int child : children)
        {
            FolderItem& entry = m_items[static_cast<size_t>(child)];
            bool anyDir = false, anyFile = false;
            for (int side = 0; side < SideCount(); ++side)
            {
                if (entry.side[side].exists)
                    (entry.side[side].isDir ? anyDir : anyFile) = true;
            }
            entry.isDir = anyDir && !anyFile;
            if (anyDir && anyFile)
                entry.note = kMismatch;
        }
        std::sort(children.begin(), children.end(), [this](int a, int b) {
            const FolderItem& x = m_items[static_cast<size_t>(a)];
            const FolderItem& y = m_items[static_cast<size_t>(b)];
            if (x.isDir != y.isDir)
                return x.isDir;
            return _wcsicmp(x.name.c_str(), y.name.c_str()) < 0;
        });
        m_items[static_cast<size_t>(item)].children = children;

        progress.scanned += static_cast<int>(children.size());
        if (report && !children.empty())
        {
            progress.current = m_items[static_cast<size_t>(item)].relPath;
            report(progress);
        }
        for (int child : children)
        {
            if (cancel && *cancel)
                return;
            const FolderItem& entry = m_items[static_cast<size_t>(child)];
            if (entry.isDir && entry.note.empty())
                Scan(child, cancel, progress, report);
        }
    }

    void FolderComparison::CompareTree(int item, const std::atomic<bool>* cancel, FolderProgress& progress, const std::function<void(const FolderProgress&)>& report)
    {
        const std::vector<int> children = m_items[static_cast<size_t>(item)].children;
        for (int child : children)
        {
            if (cancel && *cancel)
                return;
            CompareTree(child, cancel, progress, report);
        }
        if (m_items[static_cast<size_t>(item)].isDir)
        {
            Aggregate(item);
            return;
        }
        Classify(item);
        ++progress.compared;
        if (report && progress.compared % 25 == 0)
        {
            progress.current = m_items[static_cast<size_t>(item)].relPath;
            report(progress);
        }
    }

    bool FolderComparison::FilesEqual(int item, int a, int b, std::wstring& note) const
    {
        const FolderItem& entry = m_items[static_cast<size_t>(item)];
        const FolderSide& sa = entry.side[a];
        const FolderSide& sb = entry.side[b];
        if (!sa.exists || !sb.exists)
            return sa.exists == sb.exists;
        if (sa.isDir || sb.isDir)
            return sa.isDir && sb.isDir;
        return CompareFiles(Ref(item, a), sa, Ref(item, b), sb, m_options, note) == 1;
    }

    void FolderComparison::Classify(int item)
    {
        FolderItem& entry = m_items[static_cast<size_t>(item)];
        std::wstring note;
        if (!IsThreeWay())
        {
            if (entry.side[0].exists && !entry.side[1].exists)
                entry.status = ItemStatus::LeftOnly;
            else if (!entry.side[0].exists && entry.side[1].exists)
                entry.status = ItemStatus::RightOnly;
            else
                entry.status = FilesEqual(item, 0, 1, note) ? ItemStatus::Identical : ItemStatus::Different;
        }
        else
        {
            const bool leftSame = FilesEqual(item, 0, 1, note);
            const bool rightSame = FilesEqual(item, 2, 1, note);
            if (leftSame && rightSame)
                entry.status = ItemStatus::Identical;
            else if (leftSame)
                entry.status = ItemStatus::RightChanged;
            else if (rightSame)
                entry.status = ItemStatus::LeftChanged;
            else
                entry.status = FilesEqual(item, 0, 2, note) ? ItemStatus::BothSame : ItemStatus::Conflict;
        }
        if (note.compare(0, 6, L"Cannot") == 0 || note.compare(0, 4, L"Read") == 0)
            entry.status = ItemStatus::Error;
        // Folders keep their scan notes; a folder/file mismatch keeps its explanation.
        if (!entry.isDir && entry.note != kMismatch)
            entry.note = entry.status == ItemStatus::Identical && note != kIgnoredOnly ? std::wstring() : note;
    }

    void FolderComparison::Aggregate(int item)
    {
        FolderItem& entry = m_items[static_cast<size_t>(item)];
        // Existence decides first (a folder added or removed on one side).
        Classify(item);
        if (entry.status != ItemStatus::Identical)
        {
            if (IsThreeWay())
            {
                for (int child : entry.children)
                {
                    if (m_items[static_cast<size_t>(child)].status == ItemStatus::Conflict)
                        entry.status = ItemStatus::Conflict;
                }
            }
            return;
        }
        bool different = false, conflict = false;
        for (int child : entry.children)
        {
            const ItemStatus status = m_items[static_cast<size_t>(child)].status;
            different = different || status != ItemStatus::Identical;
            conflict = conflict || status == ItemStatus::Conflict;
        }
        entry.status = conflict ? ItemStatus::Conflict : (different ? ItemStatus::Different : ItemStatus::Identical);
    }

    FolderComparison::Counts FolderComparison::CountFiles() const
    {
        Counts counts;
        for (const FolderItem& item : m_items)
        {
            if (item.parent < 0 || item.isDir)
                continue;
            switch (item.status)
            {
            case ItemStatus::Identical:
                ++counts.identical;
                break;
            case ItemStatus::Different:
                ++counts.different;
                break;
            case ItemStatus::LeftOnly:
                ++counts.leftOnly;
                break;
            case ItemStatus::RightOnly:
                ++counts.rightOnly;
                break;
            case ItemStatus::LeftChanged:
                ++counts.leftChanged;
                break;
            case ItemStatus::RightChanged:
                ++counts.rightChanged;
                break;
            case ItemStatus::BothSame:
                ++counts.bothSame;
                break;
            case ItemStatus::Conflict:
                ++counts.conflicts;
                break;
            case ItemStatus::Error:
                ++counts.errors;
                break;
            default:
                break;
            }
        }
        return counts;
    }

    void FolderComparison::UpdateSide(int item, int side)
    {
        if (!IsArchiveSide(side))
        {
            m_items[static_cast<size_t>(item)].side[side] = Stat(FullPath(item, side));
            return;
        }
        // Archives do not change: the entry as listed.
        FolderSide info;
        const PackedArchive* pack = Pack(side);
        if (item == 0)
        {
            info = Stat(m_roots[static_cast<size_t>(side)]);
            info.isDir = info.exists = pack != nullptr;
        }
        else if (pack)
        {
            const int index = pack->Find(m_items[static_cast<size_t>(item)].relPath);
            if (index >= 0)
            {
                const PackedEntry& entry = pack->Entries()[static_cast<size_t>(index)];
                info.exists = true;
                info.isDir = entry.isDir;
                info.size = entry.size;
                info.modified = entry.modified;
            }
        }
        info.readOnly = true;
        m_items[static_cast<size_t>(item)].side[side] = info;
    }

    void FolderComparison::RefreshItem(int item)
    {
        FolderItem& entry = m_items[static_cast<size_t>(item)];
        for (int side = 0; side < SideCount(); ++side)
            UpdateSide(item, side);
        // Forget the old subtree (its items stay in the vector, marked unreachable).
        std::vector<int> stack = entry.children;
        while (!stack.empty())
        {
            const int child = stack.back();
            stack.pop_back();
            FolderItem& old = m_items[static_cast<size_t>(child)];
            stack.insert(stack.end(), old.children.begin(), old.children.end());
            old.parent = -2;
            old.children.clear();
        }
        bool anyDir = false, anyFile = false;
        for (int side = 0; side < SideCount(); ++side)
        {
            if (m_items[static_cast<size_t>(item)].side[side].exists)
                (m_items[static_cast<size_t>(item)].side[side].isDir ? anyDir : anyFile) = true;
        }
        m_items[static_cast<size_t>(item)].isDir = item == 0 || (anyDir && !anyFile);
        m_items[static_cast<size_t>(item)].children.clear();
        m_items[static_cast<size_t>(item)].note.clear();
        FolderProgress progress;
        if (m_items[static_cast<size_t>(item)].isDir)
            Scan(item, nullptr, progress, nullptr);
        CompareTree(item, nullptr, progress, nullptr);
        RefreshUp(m_items[static_cast<size_t>(item)].parent);
    }

    void FolderComparison::RestoreItems(std::vector<FolderItem> items)
    {
        m_items = std::move(items);
        for (FolderItem& item : m_items)
            item.children.clear();
        for (size_t i = 0; i < m_items.size(); ++i)
        {
            const int parent = m_items[i].parent;
            if (parent >= 0 && parent < static_cast<int>(i))
                m_items[static_cast<size_t>(parent)].children.push_back(static_cast<int>(i));
        }
    }

    void FolderComparison::RefreshUp(int item)
    {
        while (item >= 0)
        {
            Aggregate(item);
            item = m_items[static_cast<size_t>(item)].parent;
        }
    }

    bool FolderComparison::DeleteItem(int item, int side, std::wstring& error)
    {
        if (!m_items[static_cast<size_t>(item)].side[side].exists)
            return true;
        if (IsArchiveSide(side))
        {
            error = m_roots[static_cast<size_t>(side)] + L" is an archive; archives are compared read-only.";
            return false;
        }
        if (!DeleteToRecycleBin(FullPath(item, side), m_options.useRecycleBin, error))
            return false;
        RefreshItem(item);
        return true;
    }

    bool FolderComparison::CopyItem(int item, int from, int to, std::wstring& error)
    {
        if (from == to || item <= 0)
            return false;
        if (IsArchiveSide(to))
        {
            error = m_roots[static_cast<size_t>(to)] + L" is an archive; archives are compared read-only.";
            return false;
        }
        const FolderItem source = m_items[static_cast<size_t>(item)];
        if (!source.side[from].exists)
            return DeleteItem(item, to, error);
        // A file is copied from disk, or extracted from an archive.
        auto copyFile = [&](int file, const std::wstring& target) {
            SetFileAttributesW(LongPath(target).c_str(), FILE_ATTRIBUTE_NORMAL);
            if (IsArchiveSide(from))
            {
                std::string bytes;
                return ReadItem(file, from, bytes, error) && WriteFileBytes(LongPath(target), bytes, error);
            }
            if (!CopyFileW(LongPath(FullPath(file, from)).c_str(), LongPath(target).c_str(), FALSE))
            {
                error = L"Cannot copy to " + target + L": " + LastErrorText(GetLastError());
                return false;
            }
            return true;
        };
        const std::wstring dst = FullPath(item, to);
        // A file where a folder should go (or the reverse) is removed first.
        if (source.side[to].exists && source.side[to].isDir != source.side[from].isDir && !DeleteToRecycleBin(dst, m_options.useRecycleBin, error))
            return false;
        const size_t slash = dst.find_last_of(L"\\/");
        if (slash != std::wstring::npos && !CreateFolders(dst.substr(0, slash), error))
            return false;

        bool ok = true;
        if (source.side[from].isDir)
        {
            // Copy the folder's compared contents (filtered items stay out), keeping extra items on `to`.
            std::vector<int> stack = { item };
            if (!CreateFolders(dst, error))
                return false;
            while (!stack.empty() && ok)
            {
                const int current = stack.back();
                stack.pop_back();
                for (int child : m_items[static_cast<size_t>(current)].children)
                {
                    const FolderItem& entry = m_items[static_cast<size_t>(child)];
                    if (!entry.side[from].exists)
                        continue;
                    const std::wstring childDst = FullPath(child, to);
                    if (entry.side[from].isDir)
                    {
                        ok = CreateFolders(childDst, error);
                        stack.push_back(child);
                    }
                    else
                        ok = copyFile(child, childDst);
                    if (!ok)
                        break;
                }
            }
        }
        else
            ok = copyFile(item, dst);
        RefreshItem(item);
        return ok;
    }

    FolderComparison::MergeReport FolderComparison::AutoMerge(bool dryRun)
    {
        MergeReport report;
        if (!IsThreeWay())
            return report;
        if (IsArchiveSide(1))
        {
            report.errors.push_back(L"The ancestor is an archive: merge into a folder instead.");
            return report;
        }
        struct Action
        {
            int item;
            int from;  // -1: text merge
        };
        std::vector<Action> actions;
        std::vector<int> stack = { 0 };
        while (!stack.empty())
        {
            const int current = stack.back();
            stack.pop_back();
            const std::vector<int>& children = m_items[static_cast<size_t>(current)].children;
            for (auto it = children.rbegin(); it != children.rend(); ++it)
            {
                const FolderItem& entry = m_items[static_cast<size_t>(*it)];
                switch (entry.status)
                {
                case ItemStatus::LeftChanged:
                case ItemStatus::BothSame:
                    actions.push_back({ *it, 0 });
                    break;
                case ItemStatus::RightChanged:
                    actions.push_back({ *it, 2 });
                    break;
                case ItemStatus::Different:
                case ItemStatus::Conflict:
                    // A folder on all sides: look inside. A file changed on both sides: try a text merge.
                    if (entry.isDir && entry.side[0].exists && entry.side[1].exists && entry.side[2].exists)
                        stack.push_back(*it);
                    else if (!entry.isDir && entry.side[0].exists && entry.side[1].exists && entry.side[2].exists)
                        actions.push_back({ *it, -1 });
                    else
                        report.conflicts.push_back(entry.relPath);
                    break;
                default:
                    break;
                }
            }
        }

        for (const Action& action : actions)
        {
            const std::wstring relPath = m_items[static_cast<size_t>(action.item)].relPath;
            if (action.from >= 0)
            {
                ++report.applied;
                std::wstring error;
                if (!dryRun && !CopyItem(action.item, action.from, 1, error))
                    report.errors.push_back(relPath + L": " + error);
                continue;
            }
            // Text merge of a file changed on both sides.
            std::string bytes[3];
            std::wstring error;
            bool readable = true;
            for (int side = 0; side < 3 && readable; ++side)
                readable = ReadItem(action.item, side, bytes[side], error);
            if (!readable || LooksBinary(bytes[0]) || LooksBinary(bytes[1]) || LooksBinary(bytes[2]))
            {
                report.conflicts.push_back(relPath);
                continue;
            }
            DecodedText decoded[3];
            Lines lines[3];
            for (int side = 0; side < 3; ++side)
            {
                decoded[side] = DecodeText(bytes[side]);
                lines[side] = SplitLines(decoded[side].text);
            }
            const std::vector<Chunk> chunks = Diff3Lines(lines[0], lines[1], lines[2], m_options.text);
            const MergeResult merged = Merge3(lines[0], lines[1], lines[2], chunks, false);
            if (merged.conflicts > 0)
            {
                report.conflicts.push_back(relPath);
                continue;
            }
            ++report.textMerged;
            if (dryRun)
                continue;
            if (!WriteFileBytes(LongPath(FullPath(action.item, 1)), EncodeText(JoinLines(merged.lines), decoded[1].encoding), error))
                report.errors.push_back(relPath + L": " + error);
            RefreshItem(action.item);
            const int refreshed = FindItem(relPath);
            if (refreshed >= 0)
                m_items[static_cast<size_t>(refreshed)].note = L"Merged line by line from both sides; check it and save";
        }
        return report;
    }
}
