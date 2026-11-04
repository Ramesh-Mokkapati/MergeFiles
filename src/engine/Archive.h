// Archive.h - single-file comparison archives (.mfcmp): a file comparison saved with the files'
// current text, options, bookmarks with comments, edit/resolved marks and synchronization links;
// or a folder comparison's results. An archive can be reopened later, on any machine.
#pragma once

#include <memory>
#include <string>

#include "Comparison.h"
#include "FolderCompare.h"

namespace mf
{
    enum class ArchiveKind
    {
        None,
        Files,
        Folders,
    };

    // What an archive's text holds (None if it is not a MergeFiles archive).
    ArchiveKind ArchiveKindOf(const std::wstring& text);

    std::wstring SaveComparisonArchive(const Comparison& comparison);
    // Fills a comparison with the archived files (untitled: saving them asks for a name).
    std::unique_ptr<Comparison> LoadComparisonArchive(const std::wstring& text, std::wstring& error);

    std::wstring SaveFolderArchive(const FolderComparison& comparison);
    std::unique_ptr<FolderComparison> LoadFolderArchive(const std::wstring& text, std::wstring& error);

    bool WriteArchiveFile(const std::wstring& path, const std::wstring& text, std::wstring& error);
    bool ReadArchiveFile(const std::wstring& path, std::wstring& text, std::wstring& error);
}
