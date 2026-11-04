// Printing.h - printing and print preview of comparisons. A comparison is turned into a PrintModel
// (rows of cells, one cell per file, aligned like vertical padding, with the on-screen colours and
// character highlights), which PrintLayout paginates and draws on a printer or a preview.
#pragma once

#include "framework.h"

#include "Comparison.h"
#include "FolderCompare.h"
#include "InlineDiff.h"

namespace mf
{
    struct PrintCell
    {
        std::wstring number;  // line number ("" for none)
        std::wstring text;
        COLORREF back = RGB(255, 255, 255);
        COLORREF rangeBack = RGB(255, 255, 255);
        std::vector<CharRange> ranges;  // highlighted characters
        bool pad = false;               // a padding cell (no line on this side)
    };

    struct PrintColumn
    {
        std::wstring title;
        bool numbered = true;  // a line-number column precedes the text
        int weight = 1;        // share of the width among the columns
    };

    struct PrintRow
    {
        std::vector<PrintCell> cells;  // one per column
        bool changed = false;
        COLORREF marker = CLR_INVALID;  // drawn between the columns of a changed row (the "link")
        bool blockStart = false;
        std::wstring note;  // bookmark comments, printed under the row
    };

    struct PrintModel
    {
        std::wstring title;
        std::wstring files;  // for the {files} header token
        std::vector<PrintColumn> columns;
        std::vector<PrintRow> rows;
    };

    // Header and footer text may use {title}, {files}, {date}, {page} and {pages}.
    struct PageSettings
    {
        std::wstring header = L"{title}";
        std::wstring footer = L"{files}    Page {page} of {pages}    {date}";
        bool landscape = true;
        bool changedPagesOnly = false;
        int fontPoints = 8;
    };

    PrintModel BuildPrintModel(const Comparison& comparison);
    PrintModel BuildFolderPrintModel(const FolderComparison& comparison);

    class PrintLayout
    {
    public:
        PrintLayout(const PrintModel& model, const PageSettings& settings) : m_model(model), m_settings(settings)
        {
        }
        // Lays the rows out for pages of `page` (device units) at `dpi` dots per inch.
        void Paginate(CDC& dc, const CRect& page, int dpi);
        int PageCount() const
        {
            return static_cast<int>(m_pages.size());
        }
        // Draws page `index` (0-based among the pages that are printed).
        void DrawPage(CDC& dc, int index, const CRect& page, int dpi);

    private:
        struct Metrics
        {
            int dpi = 96;
            int lineHeight = 0;
            int charWidth = 0;
            int margin = 0;
            int headerHeight = 0;
            int columnHeader = 0;
            CRect body;
            std::vector<int> columnLeft;   // x of each column (its number area)
            std::vector<int> textLeft;     // x of each column's text
            std::vector<int> textChars;    // characters per wrapped text line
        };
        void Measure(CDC& dc, const CRect& page, int dpi);
        void MakeFonts(int dpi);
        int RowHeight(const PrintRow& row) const;
        std::wstring Expand(const std::wstring& text) const;
        std::wstring Token(const std::wstring& text, int page) const;

        const PrintModel& m_model;
        PageSettings m_settings;
        Metrics m_metrics;
        CFont m_font, m_bold, m_small;
        int m_fontDpi = 0;
        std::vector<std::pair<int, int>> m_pages;  // [first row, end row) of each printed page
        int m_totalPages = 0;
    };

    // Shows the print dialog and prints; false if cancelled or failed.
    bool PrintModelToPrinter(const PrintModel& model, const PageSettings& settings, CWnd* parent);
    // Modal print preview; the user can print from it.
    void ShowPrintPreview(const PrintModel& model, const PageSettings& settings, CWnd* parent);
}
