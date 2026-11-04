// Encoding.h - detecting, decoding and encoding text files: UTF-8 and UTF-16 (LE/BE), each with
// or without a byte order mark, and ANSI/MBCS code pages (including EUC-JP, code page 20932).
#pragma once

#include <string>

namespace mf
{
    enum class EncodingType
    {
        Utf8,
        Utf16LE,
        Utf16BE,
        CodePage  // ANSI or another MBCS code page; codePage 0 means the system ANSI code page
    };

    struct TextEncoding
    {
        EncodingType type = EncodingType::Utf8;
        bool bom = false;          // Unicode types only
        unsigned codePage = 0;     // CodePage type only

        bool operator==(const TextEncoding& other) const
        {
            return type == other.type && bom == other.bom && (type != EncodingType::CodePage || codePage == other.codePage);
        }
        bool operator!=(const TextEncoding& other) const
        {
            return !(*this == other);
        }
    };

    // "Unicode (UTF-8 with BOM)", "Unicode (UTF-16 LE)", "ANSI (Windows-1252)", ...
    std::wstring EncodingName(const TextEncoding& encoding);

    struct DecodedText
    {
        std::wstring text;
        TextEncoding encoding;
        bool lossy = false;  // some bytes were not valid in the encoding (or an odd UTF-16 byte count)
    };

    // Decodes file contents. With forced == nullptr the encoding is detected: a BOM wins; otherwise
    // valid UTF-8 is UTF-8 (pure ASCII included), text with many zero bytes in alternate positions
    // is UTF-16 without a BOM, and anything else is the system ANSI code page.
    DecodedText DecodeText(const std::string& bytes, const TextEncoding* forced = nullptr);

    // Encodes text, with a BOM if the encoding has one. lossy is set when characters had no
    // equivalent in a code page.
    std::string EncodeText(const std::wstring& text, const TextEncoding& encoding, bool* lossy = nullptr);

    // True for data that is very likely not text (NUL bytes that UTF-16 doesn't explain).
    bool LooksBinary(const std::string& bytes);

    bool ReadFileBytes(const std::wstring& path, std::string& bytes, std::wstring& error);
    // Writes through a temporary file in the same folder, so a failed save leaves the original intact.
    bool WriteFileBytes(const std::wstring& path, const std::string& bytes, std::wstring& error);
    bool IsFileReadOnly(const std::wstring& path);
    std::wstring LastErrorText(unsigned long error);
}
