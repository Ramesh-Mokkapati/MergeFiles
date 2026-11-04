// XmlFormat.cpp - a tokenizer and indenting printer for XML.
#include "XmlFormat.h"

#include <cwchar>
#include <cwctype>
#include <vector>

namespace mf
{
    namespace
    {
        enum class TokenType
        {
            Start,      // <a ...>
            End,        // </a>
            Empty,      // <a ... />
            Text,
            Other,      // comment, CDATA, <?...?>, <!DOCTYPE ...>
        };

        struct Token
        {
            TokenType type;
            std::wstring text;
        };

        std::wstring Trim(const std::wstring& text)
        {
            size_t start = 0, end = text.size();
            while (start < end && std::iswspace(text[start]))
                ++start;
            while (end > start && std::iswspace(text[end - 1]))
                --end;
            return text.substr(start, end - start);
        }

        // Collapses runs of whitespace (inside tags, between attributes) to single spaces.
        std::wstring Collapse(const std::wstring& tag)
        {
            std::wstring result;
            bool space = false;
            wchar_t quote = 0;
            for (wchar_t c : tag)
            {
                if (quote)
                {
                    result.push_back(c);
                    if (c == quote)
                        quote = 0;
                    continue;
                }
                if (c == L'"' || c == L'\'')
                    quote = c;
                if (std::iswspace(c))
                {
                    space = true;
                    continue;
                }
                if (space && !result.empty() && c != L'>')
                    result.push_back(L' ');
                space = false;
                result.push_back(c);
            }
            return result;
        }

        bool Tokenize(const std::wstring& text, std::vector<Token>& tokens, std::wstring& error)
        {
            size_t i = 0;
            const size_t n = text.size();
            if (n > 0 && text[0] == 0xFEFF)
                i = 1;
            while (i < n)
            {
                if (text[i] != L'<')
                {
                    const size_t next = text.find(L'<', i);
                    const size_t end = next == std::wstring::npos ? n : next;
                    tokens.push_back({ TokenType::Text, text.substr(i, end - i) });
                    i = end;
                    continue;
                }
                auto closeAt = [&](const wchar_t* close, const wchar_t* what) {
                    const size_t end = text.find(close, i);
                    if (end == std::wstring::npos)
                    {
                        error = std::wstring(what) + L" is not closed.";
                        return std::wstring::npos;
                    }
                    return end + wcslen(close);
                };
                size_t end;
                TokenType type = TokenType::Other;
                if (text.compare(i, 4, L"<!--") == 0)
                    end = closeAt(L"-->", L"A comment");
                else if (text.compare(i, 9, L"<![CDATA[") == 0)
                    end = closeAt(L"]]>", L"A CDATA section");
                else if (text.compare(i, 2, L"<?") == 0)
                    end = closeAt(L"?>", L"A processing instruction");
                else
                {
                    // A tag (or <!DOCTYPE ...> with an optional [internal subset]); quotes may hold '>'.
                    wchar_t quote = 0;
                    int brackets = 0;
                    end = std::wstring::npos;
                    for (size_t k = i + 1; k < n; ++k)
                    {
                        const wchar_t c = text[k];
                        if (quote)
                        {
                            if (c == quote)
                                quote = 0;
                        }
                        else if (c == L'"' || c == L'\'')
                            quote = c;
                        else if (c == L'[')
                            ++brackets;
                        else if (c == L']')
                            --brackets;
                        else if (c == L'>' && brackets <= 0)
                        {
                            end = k + 1;
                            break;
                        }
                    }
                    if (end == std::wstring::npos)
                        error = L"A tag is not closed.";
                    else if (text.compare(i, 2, L"<!") == 0)
                        type = TokenType::Other;
                    else if (text.compare(i, 2, L"</") == 0)
                        type = TokenType::End;
                    else if (end >= 2 && text[end - 2] == L'/')
                        type = TokenType::Empty;
                    else
                        type = TokenType::Start;
                }
                if (end == std::wstring::npos)
                    return false;
                std::wstring token = text.substr(i, end - i);
                if (type == TokenType::Start || type == TokenType::End || type == TokenType::Empty)
                    token = Collapse(token);
                tokens.push_back({ type, token });
                i = end;
            }
            return true;
        }
    }

    bool LooksLikeXml(const std::wstring& text)
    {
        size_t i = 0;
        if (!text.empty() && text[0] == 0xFEFF)
            i = 1;
        while (i < text.size() && std::iswspace(text[i]))
            ++i;
        return i < text.size() && text[i] == L'<';
    }

    bool FormatXml(const std::wstring& text, int indent, Eol eol, std::wstring& formatted, std::wstring& error)
    {
        std::vector<Token> tokens;
        if (!Tokenize(text, tokens, error))
            return false;
        const wchar_t* newline = EolChars(eol == Eol::None ? Eol::CrLf : eol);
        formatted.clear();
        int depth = 0;
        auto emit = [&](const std::wstring& line) {
            formatted.append(static_cast<size_t>(depth * indent), L' ');
            formatted += line;
            formatted += newline;
        };
        for (size_t t = 0; t < tokens.size(); ++t)
        {
            const Token& token = tokens[t];
            switch (token.type)
            {
            case TokenType::Start:
            {
                // <a>text</a> and <a></a> stay on one line.
                size_t next = t + 1;
                std::wstring inner;
                if (next < tokens.size() && tokens[next].type == TokenType::Text)
                {
                    inner = Trim(tokens[next].text);
                    ++next;
                }
                if (next < tokens.size() && tokens[next].type == TokenType::End && inner.find(L'\n') == std::wstring::npos)
                {
                    emit(token.text + inner + tokens[next].text);
                    t = next;
                    break;
                }
                emit(token.text);
                ++depth;
                break;
            }
            case TokenType::End:
                depth = depth > 0 ? depth - 1 : 0;
                emit(token.text);
                break;
            case TokenType::Text:
            {
                const std::wstring trimmed = Trim(token.text);
                if (!trimmed.empty())
                    emit(trimmed);
                break;
            }
            default:
                emit(token.text);
                break;
            }
        }
        return true;
    }
}
