// Report.h - comparison reports: HTML (side by side, optionally as a slideshow of the changes),
// XML and unified diff for file comparisons; HTML and XML for folder comparisons, optionally with a
// report of every changed text file. Bookmark comments are included.
#pragma once

#include <string>

#include "Comparison.h"
#include "FolderCompare.h"

namespace mf
{
    enum class ReportFormat
    {
        Html,
        HtmlSlideshow,  // one change at a time, with Previous/Next buttons and arrow keys
        Xml,
        UnifiedDiff,
    };

    struct ReportOptions
    {
        ReportFormat format = ReportFormat::Html;
        int context = -1;      // unchanged lines shown around changes; -1 = the whole files
        std::wstring title;    // default: the file names
    };

    // The report as text (write it as UTF-8).
    std::wstring MakeReport(const Comparison& comparison, const ReportOptions& options);

    // Unified diff of a two-way comparison; for three-way, ancestor->left then ancestor->right.
    std::wstring UnifiedDiffText(const Comparison& comparison, int context);

    struct FolderReportOptions
    {
        bool xml = false;
        bool onlyDifferences = true;      // leave identical files out
        bool includeFileReports = false;  // HTML: a side-by-side report of every changed text file
        int context = 3;                  // for the file reports
        std::wstring title;
    };

    std::wstring MakeFolderReport(const FolderComparison& comparison, const FolderReportOptions& options);
}
