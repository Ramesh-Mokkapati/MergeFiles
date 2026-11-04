// Xml.cpp - the minimal XML reader, escaping and base64.
#include "Xml.h"

#include <cstdint>
#include <cwchar>
#include <cwctype>

namespace mf
{
    namespace
    {
        class Reader
        {
        public:
            Reader(const std::wstring& text, std::wstring& error) : m_text(text), m_error(error)
            {
            }

            bool Document(XmlNode& root)
            {
                if (!m_text.empty() && m_text[0] == 0xFEFF)
                    ++m_pos;
                SkipMisc();
                if (!Element(root))
                    return false;
                SkipMisc();
                return true;
            }

        private:
            bool Fail(const wchar_t* message)
            {
                m_error = std::wstring(message) + L" (at character " + std::to_wstring(m_pos) + L")";
                return false;
            }

            bool StartsWith(const wchar_t* prefix) const
            {
                return m_text.compare(m_pos, wcslen(prefix), prefix) == 0;
            }

            void SkipSpace()
            {
                while (m_pos < m_text.size() && std::iswspace(m_text[m_pos]))
                    ++m_pos;
            }

            // Whitespace, comments, processing instructions and DOCTYPE between elements.
            void SkipMisc()
            {
                for (;;)
                {
                    SkipSpace();
                    if (StartsWith(L"<?"))
                        Skip(L"?>");
                    else if (StartsWith(L"<!--"))
                        Skip(L"-->");
                    else if (StartsWith(L"<!DOCTYPE"))
                        Skip(L">");
                    else
                        return;
                }
            }

            void Skip(const wchar_t* close)
            {
                const size_t end = m_text.find(close, m_pos);
                m_pos = end == std::wstring::npos ? m_text.size() : end + wcslen(close);
            }

            std::wstring Name()
            {
                const size_t start = m_pos;
                while (m_pos < m_text.size() && (std::iswalnum(m_text[m_pos]) || m_text[m_pos] == L'_' || m_text[m_pos] == L'-' || m_text[m_pos] == L':' || m_text[m_pos] == L'.'))
                    ++m_pos;
                return m_text.substr(start, m_pos - start);
            }

            bool Decode(const std::wstring& raw, std::wstring& out)
            {
                out.clear();
                for (size_t i = 0; i < raw.size(); ++i)
                {
                    if (raw[i] != L'&')
                    {
                        out.push_back(raw[i]);
                        continue;
                    }
                    const size_t semi = raw.find(L';', i);
                    if (semi == std::wstring::npos)
                        return Fail(L"Unterminated entity");
                    const std::wstring entity = raw.substr(i + 1, semi - i - 1);
                    if (entity == L"lt")
                        out.push_back(L'<');
                    else if (entity == L"gt")
                        out.push_back(L'>');
                    else if (entity == L"amp")
                        out.push_back(L'&');
                    else if (entity == L"quot")
                        out.push_back(L'"');
                    else if (entity == L"apos")
                        out.push_back(L'\'');
                    else if (entity.size() > 1 && entity[0] == L'#')
                    {
                        const unsigned long code = entity[1] == L'x' ? wcstoul(entity.c_str() + 2, nullptr, 16) : wcstoul(entity.c_str() + 1, nullptr, 10);
                        if (code >= 0x10000)
                        {
                            out.push_back(static_cast<wchar_t>(0xD800 + ((code - 0x10000) >> 10)));
                            out.push_back(static_cast<wchar_t>(0xDC00 + ((code - 0x10000) & 0x3FF)));
                        }
                        else
                        {
                            out.push_back(static_cast<wchar_t>(code));
                        }
                    }
                    else
                    {
                        return Fail(L"Unknown entity");
                    }
                    i = semi;
                }
                return true;
            }

            bool Element(XmlNode& node)
            {
                if (m_pos >= m_text.size() || m_text[m_pos] != L'<')
                    return Fail(L"An element was expected");
                ++m_pos;
                node.name = Name();
                if (node.name.empty())
                    return Fail(L"An element name was expected");
                for (;;)
                {
                    SkipSpace();
                    if (StartsWith(L"/>"))
                    {
                        m_pos += 2;
                        return true;
                    }
                    if (StartsWith(L">"))
                    {
                        ++m_pos;
                        break;
                    }
                    const std::wstring key = Name();
                    if (key.empty())
                        return Fail(L"An attribute name was expected");
                    SkipSpace();
                    if (m_pos >= m_text.size() || m_text[m_pos] != L'=')
                        return Fail(L"'=' was expected");
                    ++m_pos;
                    SkipSpace();
                    if (m_pos >= m_text.size() || (m_text[m_pos] != L'"' && m_text[m_pos] != L'\''))
                        return Fail(L"A quoted value was expected");
                    const wchar_t quote = m_text[m_pos++];
                    const size_t end = m_text.find(quote, m_pos);
                    if (end == std::wstring::npos)
                        return Fail(L"Unterminated attribute value");
                    std::wstring value;
                    if (!Decode(m_text.substr(m_pos, end - m_pos), value))
                        return false;
                    node.attributes.emplace_back(key, value);
                    m_pos = end + 1;
                }
                // Content: text, CDATA, comments and child elements, up to the end tag.
                for (;;)
                {
                    if (m_pos >= m_text.size())
                        return Fail(L"Missing end tag");
                    if (StartsWith(L"</"))
                    {
                        m_pos += 2;
                        const std::wstring closing = Name();
                        if (closing != node.name)
                            return Fail(L"Mismatched end tag");
                        SkipSpace();
                        if (m_pos >= m_text.size() || m_text[m_pos] != L'>')
                            return Fail(L"'>' was expected");
                        ++m_pos;
                        return true;
                    }
                    if (StartsWith(L"<![CDATA["))
                    {
                        const size_t end = m_text.find(L"]]>", m_pos);
                        if (end == std::wstring::npos)
                            return Fail(L"Unterminated CDATA");
                        node.text += m_text.substr(m_pos + 9, end - m_pos - 9);
                        m_pos = end + 3;
                    }
                    else if (StartsWith(L"<!--"))
                    {
                        Skip(L"-->");
                    }
                    else if (StartsWith(L"<?"))
                    {
                        Skip(L"?>");
                    }
                    else if (m_text[m_pos] == L'<')
                    {
                        node.children.emplace_back();
                        if (!Element(node.children.back()))
                            return false;
                    }
                    else
                    {
                        const size_t end = m_text.find(L'<', m_pos);
                        std::wstring decoded;
                        if (!Decode(m_text.substr(m_pos, (end == std::wstring::npos ? m_text.size() : end) - m_pos), decoded))
                            return false;
                        node.text += decoded;
                        m_pos = end == std::wstring::npos ? m_text.size() : end;
                    }
                }
            }

            const std::wstring& m_text;
            std::wstring& m_error;
            size_t m_pos = 0;
        };
    }

    const std::wstring* XmlNode::Attribute(const std::wstring& key) const
    {
        for (const auto& attribute : attributes)
        {
            if (attribute.first == key)
                return &attribute.second;
        }
        return nullptr;
    }

    std::wstring XmlNode::AttributeOr(const std::wstring& key, const std::wstring& fallback) const
    {
        const std::wstring* value = Attribute(key);
        return value ? *value : fallback;
    }

    int XmlNode::IntAttribute(const std::wstring& key, int fallback) const
    {
        const std::wstring* value = Attribute(key);
        return value && !value->empty() ? static_cast<int>(wcstol(value->c_str(), nullptr, 10)) : fallback;
    }

    const XmlNode* XmlNode::Child(const std::wstring& childName) const
    {
        for (const XmlNode& child : children)
        {
            if (child.name == childName)
                return &child;
        }
        return nullptr;
    }

    bool ParseXml(const std::wstring& text, XmlNode& root, std::wstring& error)
    {
        Reader reader(text, error);
        root = XmlNode();
        return reader.Document(root);
    }

    std::wstring XmlEscape(const std::wstring& text, bool attribute)
    {
        std::wstring out;
        out.reserve(text.size() + 16);
        for (wchar_t c : text)
        {
            switch (c)
            {
            case L'&':
                out += L"&amp;";
                break;
            case L'<':
                out += L"&lt;";
                break;
            case L'>':
                out += L"&gt;";
                break;
            case L'"':
                out += attribute ? L"&quot;" : L"\"";
                break;
            case L'\t':
            case L'\n':
            case L'\r':
                if (attribute)
                    out += c == L'\t' ? L"&#9;" : (c == L'\n' ? L"&#10;" : L"&#13;");
                else
                    out.push_back(c);
                break;
            default:
                out.push_back(c < 0x20 || c == 0xFFFE || c == 0xFFFF ? static_cast<wchar_t>(0xFFFD) : c);
                break;
            }
        }
        return out;
    }

    std::wstring Base64Encode(const std::string& bytes)
    {
        static const wchar_t* alphabet = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::wstring out;
        out.reserve((bytes.size() + 2) / 3 * 4 + bytes.size() / 57 + 2);
        size_t i = 0;
        int column = 0;
        while (i < bytes.size())
        {
            const uint32_t b0 = static_cast<unsigned char>(bytes[i]);
            const uint32_t b1 = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0;
            const uint32_t b2 = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0;
            const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
            out.push_back(alphabet[(triple >> 18) & 63]);
            out.push_back(alphabet[(triple >> 12) & 63]);
            out.push_back(i + 1 < bytes.size() ? alphabet[(triple >> 6) & 63] : L'=');
            out.push_back(i + 2 < bytes.size() ? alphabet[triple & 63] : L'=');
            i += 3;
            column += 4;
            if (column >= 76)
            {
                out.push_back(L'\n');
                column = 0;
            }
        }
        return out;
    }

    bool Base64Decode(const std::wstring& text, std::string& bytes)
    {
        bytes.clear();
        uint32_t buffer = 0;
        int bits = 0;
        for (wchar_t c : text)
        {
            int value;
            if (c >= L'A' && c <= L'Z')
                value = c - L'A';
            else if (c >= L'a' && c <= L'z')
                value = c - L'a' + 26;
            else if (c >= L'0' && c <= L'9')
                value = c - L'0' + 52;
            else if (c == L'+')
                value = 62;
            else if (c == L'/')
                value = 63;
            else if (c == L'=' || std::iswspace(c))
                continue;
            else
                return false;
            buffer = (buffer << 6) | static_cast<uint32_t>(value);
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                bytes.push_back(static_cast<char>((buffer >> bits) & 0xFF));
            }
        }
        return true;
    }
}
