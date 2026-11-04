// EncodingTests.cpp - detection and byte-exact round trips of every supported encoding, and line splitting.
#include "TestFramework.h"

#include "Encoding.h"
#include "TextLines.h"

using namespace mf;

namespace
{
    const std::wstring kSample = L"Grüße 日本語 \U0001F600\r\nline 2\nline 3\r";

    void RoundTrip(const TextEncoding& encoding)
    {
        const std::string bytes = EncodeText(kSample, encoding);
        const DecodedText decoded = DecodeText(bytes);
        CHECK(decoded.encoding == encoding);
        CHECK(decoded.text == kSample);
        CHECK(!decoded.lossy);
        CHECK(EncodeText(decoded.text, decoded.encoding) == bytes);
    }
}

TEST(Encoding, Utf8RoundTrips)
{
    RoundTrip({ EncodingType::Utf8, false, 0 });
    RoundTrip({ EncodingType::Utf8, true, 0 });
}

TEST(Encoding, Utf16RoundTrips)
{
    RoundTrip({ EncodingType::Utf16LE, true, 0 });
    RoundTrip({ EncodingType::Utf16BE, true, 0 });
}

TEST(Encoding, Utf16WithoutBomIsDetected)
{
    // Detection without a BOM needs mostly ASCII-range text.
    const std::wstring text = L"int main()\r\n{\r\n    return 0;\r\n}\r\n";
    for (EncodingType type : { EncodingType::Utf16LE, EncodingType::Utf16BE })
    {
        const TextEncoding encoding = { type, false, 0 };
        const std::string bytes = EncodeText(text, encoding);
        const DecodedText decoded = DecodeText(bytes);
        CHECK(decoded.encoding == encoding);
        CHECK(decoded.text == text);
        CHECK(!LooksBinary(bytes));
    }
}

TEST(Encoding, AnsiIsDetectedForInvalidUtf8)
{
    // "Gruß" in Windows-1252: 0xDF alone is not valid UTF-8.
    const std::string bytes = "Gru\xDF\r\n";
    const TextEncoding forced = { EncodingType::CodePage, false, 1252 };
    const DecodedText decoded = DecodeText(bytes, &forced);
    CHECK(decoded.text == L"Gruß\r\n");
    CHECK(EncodeText(decoded.text, decoded.encoding) == bytes);
    const DecodedText detected = DecodeText(bytes);
    CHECK(detected.encoding.type == EncodingType::CodePage);
}

TEST(Encoding, EucJpCodePage)
{
    const TextEncoding eucJp = { EncodingType::CodePage, false, 20932 };
    const std::wstring text = L"日本語";
    bool lossy = true;
    const std::string bytes = EncodeText(text, eucJp, &lossy);
    CHECK(!lossy);
    CHECK_EQ(bytes.size(), 6u);
    CHECK(DecodeText(bytes, &eucJp).text == text);
}

TEST(Encoding, LossyCodePageIsReported)
{
    bool lossy = false;
    EncodeText(L"日", { EncodingType::CodePage, false, 1252 }, &lossy);
    CHECK(lossy);
}

TEST(Encoding, BinaryDetection)
{
    CHECK(LooksBinary(std::string("\x89PNG\r\n\x1A\n\0\0\0\rIHDR\0\0\x01\0", 20)));
    CHECK(!LooksBinary("plain text"));
    CHECK_EQ(DecodeText("").text, std::wstring());
}

TEST(Lines, SplitAndJoinAreExactInverses)
{
    for (const wchar_t* text : { L"", L"a", L"a\n", L"a\r\nb\rc\n\nd", L"\r\n\r\n", L"x\r" })
    {
        const Lines lines = SplitLines(text);
        CHECK(!lines.empty());
        CHECK(lines.back().eol == Eol::None);
        CHECK_EQ(JoinLines(lines), std::wstring(text));
    }
    const Lines mixed = SplitLines(L"a\r\nb\nc\r\nd");
    CHECK(DominantEol(mixed) == Eol::CrLf);
    CHECK(HasMixedEol(mixed));
    Lines converted = mixed;
    ConvertEol(converted, Eol::Lf);
    CHECK_EQ(JoinLines(converted), std::wstring(L"a\nb\nc\nd"));
}
