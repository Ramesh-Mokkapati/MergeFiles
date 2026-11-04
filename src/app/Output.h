// Output.h - what the file and folder comparison windows share for getting a comparison out of the
// program: the report dialog, comparison archives, page setup, printing and the bookmark comment dialog.
#pragma once

#include "framework.h"

#include "Comparison.h"
#include "FolderCompare.h"
#include "Printing.h"

namespace mf
{
    // The report dialog's choices, remembered between reports (and sessions).
    struct ReportSettings
    {
        int format = 0;           // 0 HTML, 1 slideshow, 2 XML, 3 unified diff
        bool wholeFiles = false;  // false: only the differences with `context` lines around them
        int context = 3;
        bool fileReports = true;  // folder reports: a section for every different text file
        bool identical = false;   // folder reports: list identical items too
        bool openWhenSaved = false;
    };

    // Asks for the report's options and file name, then writes it. Exactly one of the comparisons is given.
    void CreateReport(const Comparison* files, const FolderComparison* folders, CWnd* parent);

    // Saves an archive (the text from SaveComparisonArchive or SaveFolderArchive) under a chosen name.
    void SaveArchiveAs(const std::wstring& text, const std::wstring& suggestedName, CWnd* parent);

    // Archive files: *.mfcmp.
    bool IsArchiveFileName(const std::wstring& path);
    extern const wchar_t* const kArchiveFilter;

    // Edits the header, footer, orientation and font size used for printing.
    void EditPageSetup(CWnd* parent);
    // Prints, or shows the print preview of, a model with the application's page settings.
    void PrintComparison(const PrintModel& model, bool preview, CWnd* parent);

    // The comment of a bookmark; false if cancelled.
    bool EditBookmarkComment(const CString& label, std::wstring& comment, CWnd* parent);

    // "left_vs_right" from the titles, without characters a file name cannot have.
    std::wstring SuggestedName(const std::vector<std::wstring>& names);
}
