// DocumentText.cpp - text extraction from Office Open XML, OpenDocument and RTF files.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "DocumentText.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cwctype>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "Encoding.h"
#include "Packed.h"

namespace mf
{
    namespace
    {
        std::wstring Lower(std::wstring text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            return text;
        }

        std::wstring Extension(const std::wstring& name)
        {
            const size_t dot = name.find_last_of(L'.');
            const size_t slash = name.find_last_of(L"\\/");
            if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
                return L"";
            return Lower(name.substr(dot + 1));
        }

        enum class Kind
        {
            None,
            Word,
            Excel,
            PowerPoint,
            OpenDocument,
            Rtf,
        };

        Kind KindOf(const std::wstring& name)
        {
            const std::wstring ext = Extension(name);
            static const std::pair<const wchar_t*, Kind> kinds[] = {
                { L"docx", Kind::Word }, { L"docm", Kind::Word }, { L"dotx", Kind::Word }, { L"dotm", Kind::Word },
                { L"xlsx", Kind::Excel }, { L"xlsm", Kind::Excel }, { L"xltx", Kind::Excel }, { L"xltm", Kind::Excel },
                { L"pptx", Kind::PowerPoint }, { L"pptm", Kind::PowerPoint }, { L"potx", Kind::PowerPoint }, { L"ppsx", Kind::PowerPoint },
                { L"odt", Kind::OpenDocument }, { L"ott", Kind::OpenDocument }, { L"ods", Kind::OpenDocument }, { L"ots", Kind::OpenDocument },
                { L"odp", Kind::OpenDocument }, { L"otp", Kind::OpenDocument }, { L"rtf", Kind::Rtf },
            };
            for (const auto& kind : kinds)
            {
                if (ext == kind.first)
                    return kind.second;
            }
            return Kind::None;
        }

        std::wstring FromUtf8(const std::string& bytes)
        {
            size_t start = bytes.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;
            if (bytes.size() <= start)
                return std::wstring();
            const int length = MultiByteToWideChar(CP_UTF8, 0, bytes.data() + start, static_cast<int>(bytes.size() - start), nullptr, 0);
            std::wstring text(static_cast<size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, bytes.data() + start, static_cast<int>(bytes.size() - start), text.data(), length);
            return text;
        }

        // ---- a streaming XML tokenizer: mixed content stays in document order ----

        struct Token
        {
            enum Type
            {
                Start,
                End,
                Text,
            } type = Text;
            std::wstring name;  // local name (prefix removed)
            std::vector<std::pair<std::wstring, std::wstring>> attributes;  // local names
            bool empty = false;  // <x/>: an End follows at once
            std::wstring text;

            std::wstring Attr(const wchar_t* key, const wchar_t* fallback = L"") const
            {
                for (const auto& a : attributes)
                {
                    if (a.first == key)
                        return a.second;
                }
                return fallback;
            }
        };

        std::wstring LocalName(const std::wstring& name)
        {
            const size_t colon = name.find(L':');
            return colon == std::wstring::npos ? name : name.substr(colon + 1);
        }

        void AppendCodePoint(std::wstring& out, unsigned long code)
        {
            if (code >= 0x10000 && code <= 0x10FFFF)
            {
                code -= 0x10000;
                out.push_back(static_cast<wchar_t>(0xD800 + (code >> 10)));
                out.push_back(static_cast<wchar_t>(0xDC00 + (code & 0x3FF)));
            }
            else if (code > 0 && code < 0x10000)
                out.push_back(static_cast<wchar_t>(code));
        }

        std::wstring DecodeEntities(const std::wstring& text, size_t from, size_t to)
        {
            std::wstring out;
            out.reserve(to - from);
            for (size_t i = from; i < to; ++i)
            {
                if (text[i] != L'&')
                {
                    out.push_back(text[i]);
                    continue;
                }
                const size_t semi = text.find(L';', i);
                if (semi == std::wstring::npos || semi >= to || semi - i > 10)
                {
                    out.push_back(L'&');
                    continue;
                }
                const std::wstring entity = text.substr(i + 1, semi - i - 1);
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
                else if (!entity.empty() && entity[0] == L'#')
                    AppendCodePoint(out, entity.size() > 1 && (entity[1] == L'x' || entity[1] == L'X') ? wcstoul(entity.c_str() + 2, nullptr, 16) : wcstoul(entity.c_str() + 1, nullptr, 10));
                else
                {
                    out.append(text, i, semi - i + 1);
                }
                i = semi;
            }
            return out;
        }

        bool Tokenize(const std::wstring& xml, std::vector<Token>& tokens)
        {
            size_t pos = 0;
            const size_t size = xml.size();
            auto skipTo = [&](const wchar_t* end) {
                const size_t at = xml.find(end, pos);
                pos = at == std::wstring::npos ? size : at + wcslen(end);
            };
            while (pos < size)
            {
                if (xml[pos] != L'<')
                {
                    const size_t next = (std::min)(xml.find(L'<', pos), size);
                    Token token;
                    token.text = DecodeEntities(xml, pos, next);
                    tokens.push_back(std::move(token));
                    pos = next;
                    continue;
                }
                if (xml.compare(pos, 4, L"<!--") == 0)
                    skipTo(L"-->");
                else if (xml.compare(pos, 9, L"<![CDATA[") == 0)
                {
                    const size_t end = xml.find(L"]]>", pos + 9);
                    Token token;
                    token.text = xml.substr(pos + 9, (end == std::wstring::npos ? size : end) - pos - 9);
                    tokens.push_back(std::move(token));
                    pos = end == std::wstring::npos ? size : end + 3;
                }
                else if (xml.compare(pos, 2, L"<?") == 0)
                    skipTo(L"?>");
                else if (xml.compare(pos, 2, L"<!") == 0)
                    skipTo(L">");
                else if (xml.compare(pos, 2, L"</") == 0)
                {
                    const size_t end = xml.find(L'>', pos);
                    if (end == std::wstring::npos)
                        return false;
                    Token token;
                    token.type = Token::End;
                    std::wstring name = xml.substr(pos + 2, end - pos - 2);
                    while (!name.empty() && std::iswspace(name.back()))
                        name.pop_back();
                    token.name = LocalName(name);
                    tokens.push_back(std::move(token));
                    pos = end + 1;
                }
                else
                {
                    Token token;
                    token.type = Token::Start;
                    size_t p = pos + 1;
                    while (p < size && !std::iswspace(xml[p]) && xml[p] != L'/' && xml[p] != L'>')
                        ++p;
                    token.name = LocalName(xml.substr(pos + 1, p - pos - 1));
                    for (;;)
                    {
                        while (p < size && std::iswspace(xml[p]))
                            ++p;
                        if (p >= size)
                            return false;
                        if (xml[p] == L'/')
                        {
                            token.empty = true;
                            ++p;
                            continue;
                        }
                        if (xml[p] == L'>')
                        {
                            ++p;
                            break;
                        }
                        const size_t nameStart = p;
                        while (p < size && xml[p] != L'=' && !std::iswspace(xml[p]) && xml[p] != L'>')
                            ++p;
                        std::wstring key = xml.substr(nameStart, p - nameStart);
                        while (p < size && (std::iswspace(xml[p]) || xml[p] == L'='))
                            ++p;
                        if (p >= size || (xml[p] != L'"' && xml[p] != L'\''))
                            continue;
                        const wchar_t quote = xml[p];
                        const size_t valueEnd = xml.find(quote, p + 1);
                        if (valueEnd == std::wstring::npos)
                            return false;
                        // Keep "r:id"-style keys whole as well as by local name.
                        token.attributes.emplace_back(LocalName(key), DecodeEntities(xml, p + 1, valueEnd));
                        if (key.find(L':') != std::wstring::npos)
                            token.attributes.emplace_back(key, token.attributes.back().second);
                        p = valueEnd + 1;
                    }
                    const bool empty = token.empty;
                    const std::wstring name = token.name;
                    tokens.push_back(std::move(token));
                    if (empty)
                    {
                        Token end;
                        end.type = Token::End;
                        end.name = name;
                        tokens.push_back(std::move(end));
                    }
                    pos = p;
                }
            }
            return true;
        }

        bool ReadPart(const PackedArchive& archive, const std::wstring& part, std::vector<Token>& tokens, std::wstring& error)
        {
            std::string bytes;
            if (!archive.ReadPath(part, bytes, error))
                return false;
            if (!Tokenize(FromUtf8(bytes), tokens))
            {
                error = part + L" is not well-formed XML";
                return false;
            }
            return true;
        }

        // Text output with table handling shared by the formats.
        class Writer
        {
        public:
            void Text(const std::wstring& text)
            {
                for (wchar_t c : text)
                    Char(c);
            }
            void Char(wchar_t c)
            {
                if (m_cellDepth > 0 && (c == L'\n' || c == L'\t'))
                    c = L' ';
                if (c == L'\r')
                    return;
                m_out.push_back(c);
            }
            void Paragraph()
            {
                if (m_cellDepth > 0)
                {
                    // Paragraphs inside a table cell stay on the row's line.
                    if (!m_out.empty() && m_out.back() != L'\t' && m_out.back() != L'\n' && m_out.back() != L' ')
                        m_out.push_back(L' ');
                    return;
                }
                m_out.push_back(L'\n');
            }
            void StartCell()
            {
                ++m_cellDepth;
            }
            void EndCell(int repeat = 1)
            {
                m_cellDepth = (std::max)(0, m_cellDepth - 1);
                while (!m_out.empty() && m_out.back() == L' ')
                    m_out.pop_back();
                if (m_cellDepth == 0)
                    m_out.append(static_cast<size_t>(repeat), L'\t');
            }
            void StartRow()
            {
                m_rowStart = m_out.size();
            }
            void EndRow(int repeat = 1)
            {
                if (m_cellDepth > 0)
                    return;  // a table nested in a cell
                while (!m_out.empty() && m_out.back() == L'\t')
                    m_out.pop_back();
                if (m_out.size() < m_rowStart)
                    m_rowStart = m_out.size();
                const std::wstring row = m_out.substr(m_rowStart) + L"\n";
                m_out.resize(m_rowStart);
                // Spreadsheets repeat empty rows to the end of the sheet: those are dropped.
                const bool blank = row.find_first_not_of(L" \n") == std::wstring::npos;
                const int copies = blank && repeat > 1 ? (repeat > 100 ? 0 : repeat) : (std::min)(repeat, 1000);
                for (int i = 0; i < copies; ++i)
                    m_out += row;
            }
            void Heading(const std::wstring& text)
            {
                TrimBlankEnd();
                if (!m_out.empty())
                    m_out += L"\n";
                m_out += text + L"\n";
            }
            void TrimBlankEnd()
            {
                while (m_out.size() >= 2 && m_out.back() == L'\n' && m_out[m_out.size() - 2] == L'\n')
                    m_out.pop_back();
            }
            bool InCell() const
            {
                return m_cellDepth > 0;
            }
            std::wstring Result()
            {
                TrimBlankEnd();
                while (!m_out.empty() && (m_out.back() == L'\n' || m_out.back() == L' '))
                    m_out.pop_back();
                return m_out.empty() ? m_out : m_out + L"\n";
            }
            std::wstring& Raw()
            {
                return m_out;
            }

        private:
            std::wstring m_out;
            int m_cellDepth = 0;
            size_t m_rowStart = 0;
        };

        // ---- Word ----

        void WordBody(const std::vector<Token>& tokens, Writer& out)
        {
            int skip = 0;  // inside properties, deleted text, field instructions
            std::vector<std::wstring> stack;
            static const wchar_t* skipped[] = { L"pPr", L"rPr", L"sectPr", L"tblPr", L"trPr", L"tcPr", L"tblGrid", L"delText", L"instrText", L"moveFrom", L"del", L"tabs" };
            auto isSkipped = [](const std::wstring& name) {
                return std::any_of(std::begin(skipped), std::end(skipped), [&](const wchar_t* s) { return name == s; });
            };
            for (const Token& token : tokens)
            {
                if (token.type == Token::Start)
                {
                    stack.push_back(token.name);
                    if (isSkipped(token.name))
                    {
                        ++skip;
                        continue;
                    }
                    if (skip)
                        continue;
                    if (token.name == L"tab" && !token.empty)
                        continue;
                    if (token.name == L"tab")
                        out.Char(L'\t');
                    else if (token.name == L"br" || token.name == L"cr")
                        out.Char(L'\n');
                    else if (token.name == L"noBreakHyphen")
                        out.Char(L'-');
                    else if (token.name == L"tc")
                        out.StartCell();
                    else if (token.name == L"tr")
                        out.StartRow();
                }
                else if (token.type == Token::End)
                {
                    if (!stack.empty())
                        stack.pop_back();
                    if (isSkipped(token.name))
                    {
                        skip = (std::max)(0, skip - 1);
                        continue;
                    }
                    if (skip)
                        continue;
                    if (token.name == L"p")
                        out.Paragraph();
                    else if (token.name == L"tc")
                        out.EndCell();
                    else if (token.name == L"tr")
                        out.EndRow();
                }
                else if (!skip && !stack.empty() && stack.back() == L"t")
                    out.Text(token.text);
            }
        }

        bool WordText(const PackedArchive& archive, std::wstring& text, std::wstring& error)
        {
            std::vector<Token> tokens;
            if (!ReadPart(archive, L"word/document.xml", tokens, error))
                return false;
            Writer out;
            WordBody(tokens, out);
            // Footnotes and endnotes follow the body.
            for (const wchar_t* part : { L"word/footnotes.xml", L"word/endnotes.xml" })
            {
                std::vector<Token> notes;
                std::wstring ignored;
                if (archive.Find(part) < 0 || !ReadPart(archive, part, notes, ignored))
                    continue;
                Writer noteText;
                WordBody(notes, noteText);
                const std::wstring body = noteText.Result();
                if (body.find_first_not_of(L" \n\t") != std::wstring::npos)
                {
                    out.Heading(wcsstr(part, L"foot") ? L"[Footnotes]" : L"[Endnotes]");
                    out.Raw() += body;
                }
            }
            text = out.Result();
            return true;
        }

        // ---- relationships (Office Open XML) ----

        std::map<std::wstring, std::wstring> Relationships(const PackedArchive& archive, const std::wstring& part)
        {
            // word/x.xml -> word/_rels/x.xml.rels; targets are relative to the part's folder.
            std::map<std::wstring, std::wstring> targets;
            const size_t slash = part.find_last_of(L'/');
            const std::wstring folder = slash == std::wstring::npos ? L"" : part.substr(0, slash + 1);
            const std::wstring rels = folder + L"_rels/" + part.substr(slash == std::wstring::npos ? 0 : slash + 1) + L".rels";
            std::vector<Token> tokens;
            std::wstring error;
            if (!ReadPart(archive, rels, tokens, error))
                return targets;
            for (const Token& token : tokens)
            {
                if (token.type != Token::Start || token.name != L"Relationship")
                    continue;
                std::wstring target = token.Attr(L"Target");
                if (!target.empty() && target[0] == L'/')
                    target = target.substr(1);
                else
                    target = folder + target;
                targets[token.Attr(L"Id")] = target;
            }
            return targets;
        }

        // ---- Excel ----

        int ColumnIndex(const std::wstring& reference)
        {
            int column = 0;
            for (wchar_t c : reference)
            {
                if (c >= L'A' && c <= L'Z')
                    column = column * 26 + (c - L'A' + 1);
                else if (c >= L'a' && c <= L'z')
                    column = column * 26 + (c - L'a' + 1);
                else
                    break;
            }
            return column - 1;
        }

        std::wstring CellText(std::wstring text)
        {
            for (wchar_t& c : text)
            {
                if (c == L'\t' || c == L'\r')
                    c = L' ';
            }
            std::wstring out;
            for (wchar_t c : text)
            {
                if (c == L'\n')
                    out += L" \x21B5 ";  // a line break inside a cell
                else
                    out.push_back(c);
            }
            return out;
        }

        bool ExcelText(const PackedArchive& archive, std::wstring& text, std::wstring& error)
        {
            std::vector<std::wstring> shared;
            {
                std::vector<Token> tokens;
                std::wstring ignored;
                if (archive.Find(L"xl/sharedStrings.xml") >= 0 && ReadPart(archive, L"xl/sharedStrings.xml", tokens, ignored))
                {
                    std::vector<std::wstring> stack;
                    int phonetic = 0;
                    for (const Token& token : tokens)
                    {
                        if (token.type == Token::Start)
                        {
                            stack.push_back(token.name);
                            if (token.name == L"si")
                                shared.emplace_back();
                            phonetic += token.name == L"rPh" ? 1 : 0;
                        }
                        else if (token.type == Token::End)
                        {
                            if (!stack.empty())
                                stack.pop_back();
                            phonetic -= token.name == L"rPh" && phonetic > 0 ? 1 : 0;
                        }
                        else if (!phonetic && !shared.empty() && !stack.empty() && stack.back() == L"t")
                            shared.back() += token.text;
                    }
                }
            }
            std::vector<Token> workbook;
            if (!ReadPart(archive, L"xl/workbook.xml", workbook, error))
                return false;
            const std::map<std::wstring, std::wstring> targets = Relationships(archive, L"xl/workbook.xml");
            Writer out;
            for (const Token& sheet : workbook)
            {
                if (sheet.type != Token::Start || sheet.name != L"sheet")
                    continue;
                auto target = targets.find(sheet.Attr(L"r:id"));
                if (target == targets.end())
                    target = targets.find(sheet.Attr(L"id"));
                out.Heading(L"[Sheet: " + sheet.Attr(L"name") + L"]");
                std::vector<Token> tokens;
                std::wstring ignored;
                if (target == targets.end() || !ReadPart(archive, target->second, tokens, ignored))
                    continue;
                int lastRow = 0;
                std::map<int, std::wstring> cells;
                std::wstring cellType, value;
                int column = 0;
                bool inValue = false, inInline = false;
                for (const Token& token : tokens)
                {
                    if (token.type == Token::Start)
                    {
                        if (token.name == L"row")
                        {
                            const int row = _wtoi(token.Attr(L"r", L"0").c_str());
                            // Missing rows between rows with data keep their place (up to a limit).
                            if (row > lastRow + 1 && lastRow > 0)
                                out.Raw().append(static_cast<size_t>((std::min)(row - lastRow - 1, 1000)), L'\n');
                            lastRow = row > 0 ? row : lastRow + 1;
                            cells.clear();
                        }
                        else if (token.name == L"c")
                        {
                            const std::wstring reference = token.Attr(L"r");
                            column = reference.empty() ? (cells.empty() ? 0 : cells.rbegin()->first + 1) : ColumnIndex(reference);
                            cellType = token.Attr(L"t");
                            value.clear();
                        }
                        else if (token.name == L"v")
                            inValue = true;
                        else if (token.name == L"is")
                            inInline = true;
                    }
                    else if (token.type == Token::End)
                    {
                        if (token.name == L"v")
                            inValue = false;
                        else if (token.name == L"is")
                            inInline = false;
                        else if (token.name == L"c")
                        {
                            std::wstring shown = value;
                            if (cellType == L"s")
                            {
                                const size_t index = static_cast<size_t>(_wtoi(value.c_str()));
                                shown = index < shared.size() ? shared[index] : L"";
                            }
                            else if (cellType == L"b")
                                shown = value == L"1" ? L"TRUE" : L"FALSE";
                            if (!shown.empty())
                                cells[column] = CellText(shown);
                        }
                        else if (token.name == L"row")
                        {
                            // Column n is preceded by n tabs.
                            std::wstring line;
                            int at = 0;
                            for (const auto& cell : cells)
                            {
                                line.append(static_cast<size_t>((std::max)(0, cell.first - at)), L'\t');
                                line += cell.second;
                                at = cell.first;
                            }
                            out.Raw() += line + L"\n";
                        }
                    }
                    else if (inValue || inInline)
                        value += token.text;
                }
            }
            text = out.Result();
            return true;
        }

        // ---- PowerPoint ----

        bool PowerPointText(const PackedArchive& archive, std::wstring& text, std::wstring& error)
        {
            std::vector<Token> presentation;
            if (!ReadPart(archive, L"ppt/presentation.xml", presentation, error))
                return false;
            const std::map<std::wstring, std::wstring> targets = Relationships(archive, L"ppt/presentation.xml");
            Writer out;
            int number = 0;
            for (const Token& slide : presentation)
            {
                if (slide.type != Token::Start || slide.name != L"sldId")
                    continue;
                auto target = targets.find(slide.Attr(L"r:id"));
                if (target == targets.end())
                    continue;
                out.Heading(L"[Slide " + std::to_wstring(++number) + L"]");
                std::vector<Token> tokens;
                std::wstring ignored;
                if (!ReadPart(archive, target->second, tokens, ignored))
                    continue;
                std::vector<std::wstring> stack;
                for (const Token& token : tokens)
                {
                    if (token.type == Token::Start)
                    {
                        stack.push_back(token.name);
                        if (token.name == L"br")
                            out.Char(L'\n');
                        else if (token.name == L"tc")
                            out.StartCell();
                        else if (token.name == L"tr")
                            out.StartRow();
                    }
                    else if (token.type == Token::End)
                    {
                        if (!stack.empty())
                            stack.pop_back();
                        if (token.name == L"p")
                            out.Paragraph();
                        else if (token.name == L"tc")
                            out.EndCell();
                        else if (token.name == L"tr")
                            out.EndRow();
                    }
                    else if (!stack.empty() && stack.back() == L"t")
                        out.Text(token.text);
                }
            }
            text = out.Result();
            return true;
        }

        // ---- OpenDocument ----

        bool OpenDocumentText(const PackedArchive& archive, std::wstring& text, std::wstring& error)
        {
            std::vector<Token> tokens;
            if (!ReadPart(archive, L"content.xml", tokens, error))
                return false;
            Writer out;
            int skip = 0;
            int paragraphs = 0;  // inside text:p / text:h
            int pages = 0;
            bool spreadsheet = false;
            static const wchar_t* skipped[] = { L"annotation", L"tracked-changes", L"note-citation", L"table-columns", L"table-column" };
            auto isSkipped = [](const std::wstring& name) {
                return std::any_of(std::begin(skipped), std::end(skipped), [&](const wchar_t* s) { return name == s; });
            };
            std::vector<int> cellRepeats;
            std::vector<int> rowRepeats;
            for (const Token& token : tokens)
            {
                if (token.type == Token::Start)
                {
                    if (isSkipped(token.name))
                    {
                        ++skip;
                        continue;
                    }
                    if (skip)
                        continue;
                    if (token.name == L"spreadsheet")
                        spreadsheet = true;
                    else if (token.name == L"p" || token.name == L"h")
                        ++paragraphs;
                    else if (token.name == L"s")
                        out.Raw().append(static_cast<size_t>((std::max)(1, _wtoi(token.Attr(L"c", L"1").c_str()))), L' ');
                    else if (token.name == L"tab")
                        out.Char(L'\t');
                    else if (token.name == L"line-break")
                        out.Char(L'\n');
                    else if (token.name == L"table" && spreadsheet)
                        out.Heading(L"[Sheet: " + token.Attr(L"name") + L"]");
                    else if (token.name == L"page")
                        out.Heading(L"[Slide " + std::to_wstring(++pages) + L"]");
                    else if (token.name == L"table-row")
                    {
                        out.StartRow();
                        rowRepeats.push_back((std::max)(1, _wtoi(token.Attr(L"number-rows-repeated", L"1").c_str())));
                    }
                    else if (token.name == L"table-cell" || token.name == L"covered-table-cell")
                    {
                        out.StartCell();
                        cellRepeats.push_back((std::max)(1, _wtoi(token.Attr(L"number-columns-repeated", L"1").c_str())));
                    }
                }
                else if (token.type == Token::End)
                {
                    if (isSkipped(token.name))
                    {
                        skip = (std::max)(0, skip - 1);
                        continue;
                    }
                    if (skip)
                        continue;
                    if (token.name == L"p" || token.name == L"h")
                    {
                        paragraphs = (std::max)(0, paragraphs - 1);
                        out.Paragraph();
                    }
                    else if (token.name == L"table-cell" || token.name == L"covered-table-cell")
                    {
                        const int repeat = cellRepeats.empty() ? 1 : cellRepeats.back();
                        if (!cellRepeats.empty())
                            cellRepeats.pop_back();
                        // Long runs of repeated empty cells (to the sheet's edge) are trimmed with the row.
                        out.EndCell((std::min)(repeat, 1000));
                    }
                    else if (token.name == L"table-row")
                    {
                        const int repeat = rowRepeats.empty() ? 1 : rowRepeats.back();
                        if (!rowRepeats.empty())
                            rowRepeats.pop_back();
                        out.EndRow(repeat);
                    }
                }
                else if (!skip && paragraphs > 0)
                {
                    // Whitespace in OpenDocument text collapses (text:s carries real spaces).
                    for (wchar_t c : token.text)
                    {
                        if (c == L'\n' || c == L'\r' || c == L'\t')
                            c = L' ';
                        if (c == L' ' && !out.Raw().empty() && out.Raw().back() == L' ')
                            continue;
                        out.Char(c);
                    }
                }
            }
            text = out.Result();
            return true;
        }

        // ---- RTF ----

        bool RtfText(const std::string& rtf, std::wstring& text)
        {
            struct State
            {
                bool skip = false;
                int unicodeSkip = 1;
            };
            std::vector<State> stack(1);
            UINT codePage = 1252;
            std::string pending;  // bytes waiting for conversion with the code page
            int skipChars = 0;    // characters after \u still to drop
            Writer out;
            auto flush = [&]() {
                if (pending.empty())
                    return;
                const int length = MultiByteToWideChar(codePage, 0, pending.data(), static_cast<int>(pending.size()), nullptr, 0);
                std::wstring wide(static_cast<size_t>((std::max)(0, length)), L'\0');
                MultiByteToWideChar(codePage, 0, pending.data(), static_cast<int>(pending.size()), wide.data(), length);
                out.Text(wide);
                pending.clear();
            };
            auto emit = [&](wchar_t c) {
                flush();
                if (!stack.back().skip)
                    out.Char(c);
            };
            static const char* destinations[] = { "fonttbl", "colortbl", "stylesheet", "info", "pict", "object", "header", "headerl", "headerr", "headerf", "footer",
                "footerl", "footerr", "footerf", "fldinst", "xmlnsdecl", "listtable", "listoverridetable", "rsidtbl", "generator", "themedata",
                "colorschememapping", "datastore", "latentstyles", "pgdsctbl", "revtbl", "mmathPr", "shpinst", "nonshppict", "filetbl", "bkmkstart",
                "bkmkend", "operator", "author", "title", "subject", "keywords", "comment", "company", "doccomm", "template", "background", "footnote" };
            const size_t size = rtf.size();
            size_t i = 0;
            bool inCellRow = false;
            while (i < size)
            {
                const char c = rtf[i];
                if (c == '{')
                {
                    flush();
                    stack.push_back(stack.back());
                    ++i;
                    continue;
                }
                if (c == '}')
                {
                    flush();
                    if (stack.size() > 1)
                        stack.pop_back();
                    ++i;
                    continue;
                }
                if (c == '\r' || c == '\n')
                {
                    ++i;
                    continue;
                }
                if (c != '\\')
                {
                    if (skipChars > 0)
                        --skipChars;
                    else if (!stack.back().skip)
                        pending.push_back(c);
                    ++i;
                    continue;
                }
                // A control symbol or word.
                ++i;
                if (i >= size)
                    break;
                const char s = rtf[i];
                if (!std::isalpha(static_cast<unsigned char>(s)))
                {
                    ++i;
                    if (s == '\'' && i + 2 <= size)
                    {
                        const int byte = static_cast<int>(strtol(rtf.substr(i, 2).c_str(), nullptr, 16));
                        i += 2;
                        if (skipChars > 0)
                            --skipChars;
                        else if (!stack.back().skip)
                            pending.push_back(static_cast<char>(byte));
                    }
                    else if (s == '*')
                    {
                        // An ignorable destination: skipped unless MergeFiles knows it (none here).
                        stack.back().skip = true;
                    }
                    else if (s == '\\' || s == '{' || s == '}')
                        emit(static_cast<wchar_t>(s));
                    else if (s == '~')
                        emit(L'\x00A0');
                    else if (s == '_')
                        emit(L'-');
                    else if (s == '\r' || s == '\n')
                        emit(L'\n');
                    continue;
                }
                size_t start = i;
                while (i < size && std::isalpha(static_cast<unsigned char>(rtf[i])))
                    ++i;
                const std::string word = rtf.substr(start, i - start);
                bool hasParam = false;
                long param = 0;
                if (i < size && (rtf[i] == '-' || std::isdigit(static_cast<unsigned char>(rtf[i]))))
                {
                    start = i;
                    ++i;
                    while (i < size && std::isdigit(static_cast<unsigned char>(rtf[i])))
                        ++i;
                    param = strtol(rtf.substr(start, i - start).c_str(), nullptr, 10);
                    hasParam = true;
                }
                if (i < size && rtf[i] == ' ')
                    ++i;
                if (skipChars > 0 && word != "u")
                {
                    --skipChars;
                    continue;
                }
                if (std::any_of(std::begin(destinations), std::end(destinations), [&](const char* d) { return word == d; }))
                {
                    flush();
                    stack.back().skip = true;
                }
                else if (word == "ansicpg" && hasParam && param > 0)
                    codePage = static_cast<UINT>(param);
                else if (word == "uc" && hasParam)
                    stack.back().unicodeSkip = static_cast<int>(param);
                else if (word == "u" && hasParam)
                {
                    flush();
                    if (!stack.back().skip)
                        out.Char(static_cast<wchar_t>(param < 0 ? param + 65536 : param));
                    skipChars = stack.back().unicodeSkip;
                }
                else if (word == "par" || word == "sect" || word == "page")
                {
                    flush();
                    if (!stack.back().skip)
                    {
                        if (inCellRow)
                            out.Paragraph();
                        else
                            out.Char(L'\n');
                    }
                }
                else if (word == "line")
                    emit(L'\n');
                else if (word == "tab")
                    emit(L'\t');
                else if (word == "intbl")
                {
                    if (!inCellRow && !stack.back().skip)
                    {
                        flush();
                        inCellRow = true;
                        out.StartRow();
                        out.StartCell();
                    }
                }
                else if (word == "cell" || word == "nestcell")
                {
                    flush();
                    if (inCellRow)
                    {
                        out.EndCell();
                        out.StartCell();
                    }
                }
                else if (word == "row" || word == "nestrow")
                {
                    flush();
                    if (inCellRow)
                    {
                        out.EndCell();
                        out.EndRow();
                        inCellRow = false;
                    }
                }
                else if (word == "emdash")
                    emit(L'\x2014');
                else if (word == "endash")
                    emit(L'\x2013');
                else if (word == "bullet")
                    emit(L'\x2022');
                else if (word == "lquote")
                    emit(L'\x2018');
                else if (word == "rquote")
                    emit(L'\x2019');
                else if (word == "ldblquote")
                    emit(L'\x201C');
                else if (word == "rdblquote")
                    emit(L'\x201D');
                else if (word == "emspace" || word == "enspace" || word == "qmspace")
                    emit(L' ');
            }
            flush();
            text = out.Result();
            return true;
        }
    }

    bool IsDocumentFileName(const std::wstring& name)
    {
        return KindOf(name) != Kind::None;
    }

    bool ExtractDocumentText(const std::string& bytes, const std::wstring& name, std::wstring& text, std::wstring& error)
    {
        text.clear();
        if (bytes.compare(0, 5, "{\\rtf") == 0)
            return RtfText(bytes, text);
        const Kind kind = KindOf(name);
        if (kind == Kind::Rtf)
        {
            error = L"This is not an RTF document.";
            return false;
        }
        if (kind == Kind::None)
        {
            error = L"Not a document format MergeFiles can read.";
            return false;
        }
        if (bytes.compare(0, 4, "\xD0\xCF\x11\xE0") == 0)
        {
            error = L"This file is in the old binary Office format (or encrypted); save it in the current format (.docx, .xlsx, .pptx) to compare it.";
            return false;
        }
        std::unique_ptr<PackedArchive> archive = PackedArchive::FromBytes(bytes, error);
        if (!archive)
        {
            error = L"The document cannot be read (it is not a zip package).";
            return false;
        }
        bool ok = false;
        switch (kind)
        {
        case Kind::Word:
            ok = WordText(*archive, text, error);
            break;
        case Kind::Excel:
            ok = ExcelText(*archive, text, error);
            break;
        case Kind::PowerPoint:
            ok = PowerPointText(*archive, text, error);
            break;
        case Kind::OpenDocument:
            ok = OpenDocumentText(*archive, text, error);
            break;
        default:
            break;
        }
        return ok;
    }

    bool ReadDocumentText(const std::wstring& path, std::wstring& text, std::wstring& error)
    {
        std::string bytes;
        if (!ReadFileBytes(path, bytes, error))
            return false;
        return ExtractDocumentText(bytes, path, text, error);
    }
}
