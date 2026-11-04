// Syntax.cpp - per-line tokenizers for C-family languages, Python, XML/HTML, INI, SQL, PowerShell
// and batch files.
#include "Syntax.h"

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <unordered_set>

namespace mf
{
    namespace
    {
        enum State
        {
            Normal = 0,
            BlockComment = 1,     // /* ... */
            TripleDouble = 2,     // Python """ ... """
            TripleSingle = 3,     // Python ''' ... '''
            XmlComment = 4,       // <!-- ... -->
            XmlTag = 5,           // inside <tag ...> across lines
            PsBlockComment = 6,   // PowerShell <# ... #>
        };

        using WordSet = std::unordered_set<std::wstring>;

        const WordSet& Keywords(Language language)
        {
            static const WordSet cpp = { L"alignas", L"alignof", L"and", L"asm", L"auto", L"break", L"case", L"catch", L"class",
                L"concept", L"const", L"consteval", L"constexpr", L"constinit", L"const_cast", L"continue", L"co_await",
                L"co_return", L"co_yield", L"decltype", L"default", L"delete", L"do", L"dynamic_cast", L"else", L"enum",
                L"explicit", L"export", L"extern", L"false", L"final", L"for", L"friend", L"goto", L"if", L"inline",
                L"mutable", L"namespace", L"new", L"noexcept", L"not", L"nullptr", L"operator", L"or", L"override",
                L"private", L"protected", L"public", L"register", L"reinterpret_cast", L"requires", L"return", L"sizeof",
                L"static", L"static_assert", L"static_cast", L"struct", L"switch", L"template", L"this", L"thread_local",
                L"throw", L"true", L"try", L"typedef", L"typeid", L"typename", L"union", L"using", L"virtual",
                L"volatile", L"while", L"NULL", L"TRUE", L"FALSE" };
            static const WordSet csharp = { L"abstract", L"as", L"async", L"await", L"base", L"break", L"case", L"catch",
                L"checked", L"class", L"const", L"continue", L"default", L"delegate", L"do", L"else", L"enum", L"event",
                L"explicit", L"extern", L"false", L"finally", L"fixed", L"for", L"foreach", L"get", L"goto", L"if",
                L"implicit", L"in", L"interface", L"internal", L"is", L"lock", L"namespace", L"new", L"null",
                L"operator", L"out", L"override", L"params", L"private", L"protected", L"public", L"readonly",
                L"record", L"ref", L"return", L"sealed", L"set", L"sizeof", L"stackalloc", L"static", L"struct",
                L"switch", L"this", L"throw", L"true", L"try", L"typeof", L"unchecked", L"unsafe", L"using", L"var",
                L"virtual", L"void", L"volatile", L"when", L"where", L"while", L"yield" };
            static const WordSet java = { L"abstract", L"assert", L"break", L"case", L"catch", L"class", L"const",
                L"continue", L"default", L"do", L"else", L"enum", L"extends", L"false", L"final", L"finally", L"for",
                L"goto", L"if", L"implements", L"import", L"instanceof", L"interface", L"native", L"new", L"null",
                L"package", L"private", L"protected", L"public", L"record", L"return", L"static", L"strictfp", L"super",
                L"switch", L"synchronized", L"this", L"throw", L"throws", L"transient", L"true", L"try", L"var",
                L"void", L"volatile", L"while", L"yield" };
            static const WordSet javascript = { L"async", L"await", L"break", L"case", L"catch", L"class", L"const",
                L"continue", L"debugger", L"default", L"delete", L"do", L"else", L"enum", L"export", L"extends",
                L"false", L"finally", L"for", L"from", L"function", L"if", L"implements", L"import", L"in",
                L"instanceof", L"interface", L"let", L"new", L"null", L"of", L"private", L"protected", L"public",
                L"return", L"static", L"super", L"switch", L"this", L"throw", L"true", L"try", L"type", L"typeof",
                L"undefined", L"var", L"void", L"while", L"with", L"yield" };
            static const WordSet python = { L"False", L"None", L"True", L"and", L"as", L"assert", L"async", L"await",
                L"break", L"class", L"continue", L"def", L"del", L"elif", L"else", L"except", L"finally", L"for",
                L"from", L"global", L"if", L"import", L"in", L"is", L"lambda", L"match", L"case", L"nonlocal", L"not",
                L"or", L"pass", L"raise", L"return", L"self", L"try", L"while", L"with", L"yield" };
            static const WordSet sql = { L"add", L"all", L"alter", L"and", L"as", L"asc", L"begin", L"between", L"by",
                L"case", L"check", L"column", L"commit", L"constraint", L"create", L"database", L"default", L"delete",
                L"desc", L"distinct", L"drop", L"else", L"end", L"exec", L"exists", L"foreign", L"from", L"full",
                L"group", L"having", L"if", L"in", L"index", L"inner", L"insert", L"into", L"is", L"join", L"key",
                L"left", L"like", L"limit", L"not", L"null", L"on", L"or", L"order", L"outer", L"primary",
                L"procedure", L"references", L"right", L"rollback", L"select", L"set", L"table", L"then", L"top",
                L"transaction", L"union", L"unique", L"update", L"values", L"view", L"when", L"where", L"with" };
            static const WordSet powershell = { L"begin", L"break", L"catch", L"class", L"continue", L"data", L"do",
                L"dynamicparam", L"else", L"elseif", L"end", L"enum", L"exit", L"filter", L"finally", L"for",
                L"foreach", L"from", L"function", L"if", L"in", L"param", L"process", L"return", L"switch", L"throw",
                L"trap", L"try", L"until", L"using", L"while", L"-eq", L"-ne", L"-gt", L"-lt", L"-ge", L"-le",
                L"-and", L"-or", L"-not", L"-like", L"-match" };
            static const WordSet batch = { L"call", L"cd", L"copy", L"del", L"do", L"echo", L"else", L"endlocal",
                L"errorlevel", L"exist", L"exit", L"for", L"goto", L"if", L"in", L"md", L"mkdir", L"move", L"not",
                L"pause", L"pushd", L"popd", L"rd", L"ren", L"set", L"setlocal", L"shift", L"start", L"type" };
            static const WordSet none;
            switch (language)
            {
            case Language::Cpp:
                return cpp;
            case Language::CSharp:
                return csharp;
            case Language::Java:
                return java;
            case Language::JavaScript:
                return javascript;
            case Language::Python:
                return python;
            case Language::Sql:
                return sql;
            case Language::PowerShell:
                return powershell;
            case Language::Batch:
                return batch;
            default:
                return none;
            }
        }

        const WordSet& Types(Language language)
        {
            static const WordSet cpp = { L"bool", L"char", L"char8_t", L"char16_t", L"char32_t", L"double", L"float",
                L"int", L"long", L"short", L"signed", L"unsigned", L"void", L"wchar_t", L"size_t", L"ptrdiff_t",
                L"int8_t", L"int16_t", L"int32_t", L"int64_t", L"uint8_t", L"uint16_t", L"uint32_t", L"uint64_t",
                L"BOOL", L"BYTE", L"WORD", L"DWORD", L"UINT", L"LONG", L"HANDLE", L"HWND", L"LPCWSTR", L"LPWSTR",
                L"CString", L"std", L"string", L"wstring", L"vector", L"map", L"unique_ptr", L"shared_ptr" };
            static const WordSet csharp = { L"bool", L"byte", L"char", L"decimal", L"double", L"dynamic", L"float",
                L"int", L"long", L"object", L"sbyte", L"short", L"string", L"uint", L"ulong", L"ushort" };
            static const WordSet java = { L"boolean", L"byte", L"char", L"double", L"float", L"int", L"long", L"short",
                L"String", L"Object", L"Integer", L"List", L"Map" };
            static const WordSet sql = { L"bigint", L"bit", L"char", L"date", L"datetime", L"decimal", L"float", L"int",
                L"integer", L"money", L"nchar", L"numeric", L"nvarchar", L"real", L"smallint", L"text", L"time",
                L"varchar" };
            static const WordSet none;
            switch (language)
            {
            case Language::Cpp:
                return cpp;
            case Language::CSharp:
                return csharp;
            case Language::Java:
                return java;
            case Language::Sql:
                return sql;
            default:
                return none;
            }
        }

        bool CaseInsensitive(Language language)
        {
            return language == Language::Sql || language == Language::PowerShell || language == Language::Batch;
        }

        bool IsIdentStart(wchar_t c)
        {
            return c == L'_' || std::iswalpha(c);
        }

        bool IsIdentChar(wchar_t c)
        {
            return c == L'_' || std::iswalnum(c);
        }

        bool StartsWith(const std::wstring& line, size_t at, const wchar_t* text)
        {
            for (size_t k = 0; text[k]; ++k)
            {
                if (at + k >= line.size() || line[at + k] != text[k])
                    return false;
            }
            return true;
        }

        void Add(std::vector<SyntaxSpan>& spans, size_t start, size_t end, TokenClass cls)
        {
            if (end > start)
                spans.push_back({ static_cast<int>(start), static_cast<int>(end - start), cls });
        }

        // Finds the end of a block that closes with `close`; returns npos when it continues.
        size_t FindClose(const std::wstring& line, size_t from, const wchar_t* close)
        {
            const size_t at = line.find(close, from);
            return at == std::wstring::npos ? at : at + std::char_traits<wchar_t>::length(close);
        }

        size_t SkipString(const std::wstring& line, size_t i, wchar_t quote, bool escapes)
        {
            ++i;
            while (i < line.size())
            {
                if (escapes && line[i] == L'\\')
                {
                    i += 2;
                    continue;
                }
                if (line[i] == quote)
                {
                    // SQL and PowerShell write a quote inside single quotes by doubling it.
                    if (!escapes && i + 1 < line.size() && line[i + 1] == quote)
                    {
                        i += 2;
                        continue;
                    }
                    return i + 1;
                }
                ++i;
            }
            return line.size();
        }

        int HighlightCode(Language language, const std::wstring& line, int state, std::vector<SyntaxSpan>& spans)
        {
            const size_t n = line.size();
            size_t i = 0;
            const bool cFamily = language == Language::Cpp || language == Language::CSharp || language == Language::Java || language == Language::JavaScript;
            const bool hashComment = language == Language::Python || language == Language::PowerShell;
            const WordSet& keywords = Keywords(language);
            const WordSet& types = Types(language);
            const bool foldCase = CaseInsensitive(language);

            if (state == BlockComment || state == PsBlockComment)
            {
                const size_t end = FindClose(line, 0, state == BlockComment ? L"*/" : L"#>");
                if (end == std::wstring::npos)
                {
                    Add(spans, 0, n, TokenClass::Comment);
                    return state;
                }
                Add(spans, 0, end, TokenClass::Comment);
                i = end;
                state = Normal;
            }
            else if (state == TripleDouble || state == TripleSingle)
            {
                const size_t end = FindClose(line, 0, state == TripleDouble ? L"\"\"\"" : L"'''");
                if (end == std::wstring::npos)
                {
                    Add(spans, 0, n, TokenClass::String);
                    return state;
                }
                Add(spans, 0, end, TokenClass::String);
                i = end;
                state = Normal;
            }

            // A preprocessor line (C, C++, C#): the whole line, apart from trailing comments.
            if (state == Normal && (language == Language::Cpp || language == Language::CSharp))
            {
                size_t first = line.find_first_not_of(L" \t", i);
                if (first != std::wstring::npos && line[first] == L'#')
                {
                    size_t end = n;
                    const size_t comment = line.find(L"//", first);
                    const size_t block = line.find(L"/*", first);
                    end = (std::min)({ end, comment, block });
                    Add(spans, first, end, TokenClass::Preprocessor);
                    i = end;
                }
            }

            if (language == Language::Batch)
            {
                size_t first = line.find_first_not_of(L" \t@");
                if (first != std::wstring::npos && (StartsWith(line, first, L"::") || (_wcsnicmp(line.c_str() + first, L"rem", 3) == 0 && (first + 3 == n || line[first + 3] == L' '))))
                {
                    Add(spans, first, n, TokenClass::Comment);
                    return Normal;
                }
            }

            while (i < n)
            {
                const wchar_t c = line[i];
                if (cFamily && StartsWith(line, i, L"//"))
                {
                    Add(spans, i, n, TokenClass::Comment);
                    return Normal;
                }
                if ((cFamily || language == Language::Sql) && StartsWith(line, i, L"/*"))
                {
                    const size_t end = FindClose(line, i + 2, L"*/");
                    if (end == std::wstring::npos)
                    {
                        Add(spans, i, n, TokenClass::Comment);
                        return BlockComment;
                    }
                    Add(spans, i, end, TokenClass::Comment);
                    i = end;
                    continue;
                }
                if (language == Language::PowerShell && StartsWith(line, i, L"<#"))
                {
                    const size_t end = FindClose(line, i + 2, L"#>");
                    if (end == std::wstring::npos)
                    {
                        Add(spans, i, n, TokenClass::Comment);
                        return PsBlockComment;
                    }
                    Add(spans, i, end, TokenClass::Comment);
                    i = end;
                    continue;
                }
                if ((hashComment && c == L'#') || (language == Language::Sql && StartsWith(line, i, L"--")))
                {
                    Add(spans, i, n, TokenClass::Comment);
                    return Normal;
                }
                if (language == Language::Python && (StartsWith(line, i, L"\"\"\"") || StartsWith(line, i, L"'''")))
                {
                    const wchar_t* close = c == L'"' ? L"\"\"\"" : L"'''";
                    const size_t end = FindClose(line, i + 3, close);
                    if (end == std::wstring::npos)
                    {
                        Add(spans, i, n, TokenClass::String);
                        return c == L'"' ? TripleDouble : TripleSingle;
                    }
                    Add(spans, i, end, TokenClass::String);
                    i = end;
                    continue;
                }
                if (c == L'"' || (c == L'\'' && language != Language::Batch) || (c == L'`' && language == Language::JavaScript))
                {
                    const bool escapes = !(language == Language::Sql || (language == Language::PowerShell && c == L'\''));
                    const size_t end = SkipString(line, i, c, escapes);
                    Add(spans, i, end, TokenClass::String);
                    i = end;
                    continue;
                }
                if (std::iswdigit(c) || (c == L'.' && i + 1 < n && std::iswdigit(line[i + 1])))
                {
                    size_t end = i + 1;
                    while (end < n && (IsIdentChar(line[end]) || line[end] == L'.' || (line[end] == L'\'' && language == Language::Cpp)))
                        ++end;
                    Add(spans, i, end, TokenClass::Number);
                    i = end;
                    continue;
                }
                if (IsIdentStart(c) || (language == Language::PowerShell && c == L'-' && i + 1 < n && std::iswalpha(line[i + 1])) || (language == Language::PowerShell && c == L'$'))
                {
                    size_t end = i + 1;
                    while (end < n && (IsIdentChar(line[end]) || (language == Language::PowerShell && line[end] == L'-' && c != L'$')))
                        ++end;
                    std::wstring word = line.substr(i, end - i);
                    if (foldCase)
                        std::transform(word.begin(), word.end(), word.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
                    if (c == L'$')
                        Add(spans, i, end, TokenClass::Attribute);
                    else if (keywords.count(word))
                        Add(spans, i, end, TokenClass::Keyword);
                    else if (types.count(word))
                        Add(spans, i, end, TokenClass::Type);
                    i = end;
                    continue;
                }
                ++i;
            }
            return Normal;
        }

        int HighlightXml(const std::wstring& line, int state, std::vector<SyntaxSpan>& spans)
        {
            const size_t n = line.size();
            size_t i = 0;
            while (i < n)
            {
                if (state == XmlComment)
                {
                    const size_t end = FindClose(line, i, L"-->");
                    if (end == std::wstring::npos)
                    {
                        Add(spans, i, n, TokenClass::Comment);
                        return XmlComment;
                    }
                    Add(spans, i, end, TokenClass::Comment);
                    i = end;
                    state = Normal;
                    continue;
                }
                if (state == XmlTag)
                {
                    const wchar_t c = line[i];
                    if (c == L'>' || StartsWith(line, i, L"/>") || StartsWith(line, i, L"?>"))
                    {
                        const size_t end = c == L'>' ? i + 1 : i + 2;
                        Add(spans, i, end, TokenClass::Tag);
                        i = end;
                        state = Normal;
                    }
                    else if (c == L'"' || c == L'\'')
                    {
                        const size_t end = SkipString(line, i, c, false);
                        Add(spans, i, end, TokenClass::String);
                        i = end;
                    }
                    else if (IsIdentStart(c))
                    {
                        size_t end = i + 1;
                        while (end < n && (IsIdentChar(line[end]) || line[end] == L'-' || line[end] == L':' || line[end] == L'.'))
                            ++end;
                        Add(spans, i, end, TokenClass::Attribute);
                        i = end;
                    }
                    else
                    {
                        ++i;
                    }
                    continue;
                }
                if (StartsWith(line, i, L"<!--"))
                {
                    state = XmlComment;
                    continue;
                }
                if (line[i] == L'<')
                {
                    size_t end = i + 1;
                    if (end < n && (line[end] == L'/' || line[end] == L'?' || line[end] == L'!'))
                        ++end;
                    while (end < n && (IsIdentChar(line[end]) || line[end] == L'-' || line[end] == L':' || line[end] == L'.'))
                        ++end;
                    Add(spans, i, end, TokenClass::Tag);
                    i = end;
                    state = XmlTag;
                    continue;
                }
                if (line[i] == L'&')
                {
                    const size_t semi = line.find(L';', i);
                    if (semi != std::wstring::npos && semi - i < 12)
                    {
                        Add(spans, i, semi + 1, TokenClass::Number);
                        i = semi + 1;
                        continue;
                    }
                }
                ++i;
            }
            return state;
        }

        int HighlightIni(const std::wstring& line, std::vector<SyntaxSpan>& spans)
        {
            const size_t first = line.find_first_not_of(L" \t");
            if (first == std::wstring::npos)
                return Normal;
            if (line[first] == L';' || line[first] == L'#')
            {
                Add(spans, first, line.size(), TokenClass::Comment);
            }
            else if (line[first] == L'[')
            {
                const size_t close = line.find(L']', first);
                Add(spans, first, close == std::wstring::npos ? line.size() : close + 1, TokenClass::Tag);
            }
            else
            {
                const size_t equals = line.find_first_of(L"=:", first);
                if (equals != std::wstring::npos)
                {
                    Add(spans, first, equals, TokenClass::Attribute);
                    const size_t value = line.find_first_not_of(L" \t", equals + 1);
                    if (value != std::wstring::npos && (line[value] == L'"' || line[value] == L'\''))
                        Add(spans, value, SkipString(line, value, line[value], false), TokenClass::String);
                }
            }
            return Normal;
        }
    }

    Language LanguageFromPath(const std::wstring& path)
    {
        const size_t dot = path.find_last_of(L'.');
        const size_t slash = path.find_last_of(L"\\/");
        if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
            return Language::PlainText;
        std::wstring ext = path.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        static const struct
        {
            const wchar_t* ext;
            Language language;
        } map[] = {
            { L"c", Language::Cpp }, { L"cpp", Language::Cpp }, { L"cc", Language::Cpp }, { L"cxx", Language::Cpp },
            { L"h", Language::Cpp }, { L"hpp", Language::Cpp }, { L"hxx", Language::Cpp }, { L"inl", Language::Cpp },
            { L"ipp", Language::Cpp }, { L"rc", Language::Cpp }, { L"idl", Language::Cpp }, { L"m", Language::Cpp },
            { L"mm", Language::Cpp }, { L"cs", Language::CSharp }, { L"java", Language::Java }, { L"scala", Language::Java },
            { L"kt", Language::Java }, { L"js", Language::JavaScript }, { L"mjs", Language::JavaScript },
            { L"ts", Language::JavaScript }, { L"tsx", Language::JavaScript }, { L"jsx", Language::JavaScript },
            { L"json", Language::JavaScript }, { L"py", Language::Python }, { L"pyw", Language::Python },
            { L"xml", Language::Xml }, { L"xaml", Language::Xml }, { L"xsd", Language::Xml }, { L"xsl", Language::Xml },
            { L"xslt", Language::Xml }, { L"html", Language::Xml }, { L"htm", Language::Xml }, { L"xhtml", Language::Xml },
            { L"svg", Language::Xml }, { L"vcxproj", Language::Xml }, { L"csproj", Language::Xml }, { L"props", Language::Xml },
            { L"targets", Language::Xml }, { L"config", Language::Xml }, { L"resx", Language::Xml }, { L"manifest", Language::Xml },
            { L"ini", Language::Ini }, { L"cfg", Language::Ini }, { L"inf", Language::Ini }, { L"properties", Language::Ini },
            { L"toml", Language::Ini }, { L"editorconfig", Language::Ini }, { L"sql", Language::Sql },
            { L"ps1", Language::PowerShell }, { L"psm1", Language::PowerShell }, { L"psd1", Language::PowerShell },
            { L"bat", Language::Batch }, { L"cmd", Language::Batch },
        };
        for (const auto& entry : map)
        {
            if (ext == entry.ext)
                return entry.language;
        }
        return Language::PlainText;
    }

    const wchar_t* LanguageName(Language language)
    {
        switch (language)
        {
        case Language::Cpp:
            return L"C/C++";
        case Language::CSharp:
            return L"C#";
        case Language::Java:
            return L"Java";
        case Language::JavaScript:
            return L"JavaScript";
        case Language::Python:
            return L"Python";
        case Language::Xml:
            return L"XML/HTML";
        case Language::Ini:
            return L"INI";
        case Language::Sql:
            return L"SQL";
        case Language::PowerShell:
            return L"PowerShell";
        case Language::Batch:
            return L"Batch";
        default:
            return L"Plain text";
        }
    }

    int HighlightLine(Language language, const std::wstring& line, int state, std::vector<SyntaxSpan>& spans)
    {
        spans.clear();
        switch (language)
        {
        case Language::PlainText:
            return Normal;
        case Language::Xml:
            return HighlightXml(line, state, spans);
        case Language::Ini:
            return HighlightIni(line, spans);
        default:
            return HighlightCode(language, line, state, spans);
        }
    }
}
