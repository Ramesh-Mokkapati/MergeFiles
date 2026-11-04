// XmlFormat.h - lays out densely formatted XML (or XHTML) one element per line, indented by
// nesting, so differences between XML files show up as clear line differences.
#pragma once

#include <string>

#include "TextLines.h"

namespace mf
{
    // Elements that hold only text stay on one line ("<name>value</name>"). Comments, CDATA,
    // processing instructions and DOCTYPE are kept as they are. Not a validating parser: returns
    // false (with error) only when a tag, comment or CDATA section is not closed.
    bool FormatXml(const std::wstring& text, int indent, Eol eol, std::wstring& formatted, std::wstring& error);

    // True when the text looks like XML (starts with '<' after a BOM and whitespace).
    bool LooksLikeXml(const std::wstring& text);
}
