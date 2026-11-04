// DocumentText.h - the text of word-processor, spreadsheet and presentation files, for comparing
// documents as text: Word (.docx), Excel (.xlsx), PowerPoint (.pptx), OpenDocument (.odt, .ods,
// .odp) and RTF. Built in: Office Open XML and OpenDocument files are zip archives of XML parts.
//
// Layout of the text: one paragraph per line; table cells separated by tabs, one table row per
// line; each spreadsheet starts with a "[Sheet: name]" line and each slide with "[Slide n]".
#pragma once

#include <string>

namespace mf
{
    // True for the extensions above (and their macro-enabled and template variants).
    bool IsDocumentFileName(const std::wstring& name);

    // `name` picks the format (its extension); RTF content is recognised whatever the name.
    bool ExtractDocumentText(const std::string& bytes, const std::wstring& name, std::wstring& text, std::wstring& error);
    bool ReadDocumentText(const std::wstring& path, std::wstring& text, std::wstring& error);
}
