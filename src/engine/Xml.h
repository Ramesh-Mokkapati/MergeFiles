// Xml.h - a minimal XML document reader and writing helpers, enough for MergeFiles' own archives
// and reports (elements, attributes, text, CDATA, character and standard entities).
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace mf
{
    struct XmlNode
    {
        std::wstring name;
        std::vector<std::pair<std::wstring, std::wstring>> attributes;
        std::wstring text;  // the element's own text (children's text not included)
        std::vector<XmlNode> children;

        const std::wstring* Attribute(const std::wstring& key) const;
        std::wstring AttributeOr(const std::wstring& key, const std::wstring& fallback) const;
        int IntAttribute(const std::wstring& key, int fallback) const;
        const XmlNode* Child(const std::wstring& childName) const;
    };

    // Parses a document; root receives the top element.
    bool ParseXml(const std::wstring& text, XmlNode& root, std::wstring& error);

    // Escapes &, <, > (and " in attributes); characters XML 1.0 forbids become U+FFFD.
    std::wstring XmlEscape(const std::wstring& text, bool attribute = false);

    std::wstring Base64Encode(const std::string& bytes);
    bool Base64Decode(const std::wstring& text, std::string& bytes);
}
