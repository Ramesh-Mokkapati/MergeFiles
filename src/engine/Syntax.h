// Syntax.h - lightweight syntax highlighting: a per-line tokenizer that carries a small state
// (inside a block comment, a multi-line string, ...) from one line to the next.
#pragma once

#include <string>
#include <vector>

namespace mf
{
    enum class Language
    {
        PlainText,
        Cpp,
        CSharp,
        Java,
        JavaScript,
        Python,
        Xml,  // also HTML
        Ini,
        Sql,
        PowerShell,
        Batch,
    };

    enum class TokenClass : unsigned char
    {
        Text,
        Keyword,
        Type,
        Comment,
        String,
        Number,
        Preprocessor,
        Tag,        // XML/HTML element names; INI section headers
        Attribute,  // XML/HTML attribute names; INI keys
    };

    struct SyntaxSpan
    {
        int start = 0;
        int length = 0;
        TokenClass cls = TokenClass::Text;
    };

    Language LanguageFromPath(const std::wstring& path);
    const wchar_t* LanguageName(Language language);

    // Tokenizes one line. state is 0 at the start of a file; the return value is the state at the
    // start of the next line. Only non-Text spans are reported.
    int HighlightLine(Language language, const std::wstring& line, int state, std::vector<SyntaxSpan>& spans);
}
