// Compare.cpp - the comparison pipeline: normalizing lines (whitespace, case, line endings, line
// expressions), diffing between synchronization points, then marking blocks that only differ in
// ignored lines or in numbers within the tolerance.
#include "Diff.h"
#include "Diff3.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>

namespace mf
{
    namespace
    {
        bool IsSpace(wchar_t c)
        {
            return c == L' ' || c == L'\t' || c == L'\f' || c == L'\v' || c == 0x00A0 || c == 0x3000;
        }

        bool IsBlank(const std::wstring& text)
        {
            return std::all_of(text.begin(), text.end(), IsSpace);
        }

        std::wstring Widen(const char* text)
        {
            std::wstring wide;
            for (const char* p = text; *p; ++p)
                wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(*p)));
            return wide;
        }

        // Whitespace, case and line-ending normalization of one line's text.
        std::wstring NormalizeText(const std::wstring& text, Eol eol, const CompareOptions& options)
        {
            std::wstring key;
            key.reserve(text.size() + 2);
            if (options.whitespace == WhitespaceMode::Compare)
            {
                key = text;
            }
            else
            {
                bool pendingSpace = false;
                for (wchar_t c : text)
                {
                    if (IsSpace(c))
                    {
                        pendingSpace = true;
                        continue;
                    }
                    if (pendingSpace && options.whitespace == WhitespaceMode::IgnoreChanges && !key.empty())
                        key.push_back(L' ');
                    pendingSpace = false;
                    key.push_back(c);
                }
            }
            if (options.ignoreCase)
            {
                for (wchar_t& c : key)
                    c = static_cast<wchar_t>(std::towlower(c));
            }
            if (!options.ignoreEol)
            {
                key.push_back(L'\x1F');
                key += EolChars(eol);
            }
            return key;
        }

        // A number starts here: a digit, or a sign or point followed by a digit, not inside a word.
        bool NumberStartsAt(const std::wstring& text, size_t i)
        {
            if (i > 0 && (std::iswalnum(text[i - 1]) || text[i - 1] == L'_' || text[i - 1] == L'.'))
                return false;
            const wchar_t c = text[i];
            if (std::iswdigit(c))
                return true;
            if ((c == L'-' || c == L'+' || c == L'.') && i + 1 < text.size())
            {
                const wchar_t next = text[i + 1];
                return std::iswdigit(next) || (c != L'.' && next == L'.' && i + 2 < text.size() && std::iswdigit(text[i + 2]));
            }
            return false;
        }

        bool ParseNumber(const std::wstring& text, size_t& i, double& value)
        {
            if (!NumberStartsAt(text, i))
                return false;
            wchar_t* end = nullptr;
            value = std::wcstod(text.c_str() + i, &end);
            const size_t used = static_cast<size_t>(end - (text.c_str() + i));
            if (used == 0)
                return false;
            i += used;
            return true;
        }

        void MarkIgnored(std::vector<Chunk>& chunks, const std::vector<std::vector<char>>& ignorable)
        {
            const int files = static_cast<int>(ignorable.size());
            for (Chunk& chunk : chunks)
            {
                if (chunk.kind == ChunkKind::Equal)
                    continue;
                bool all = true;
                for (int file = 0; file < files && all; ++file)
                {
                    // In a three-way chunk changed on one side only, the other side equals the ancestor.
                    if ((chunk.kind == ChunkKind::LeftOnly && file == 2) || (chunk.kind == ChunkKind::RightOnly && file == 0))
                        continue;
                    for (int line = chunk.start[file]; line < chunk.End(file) && all; ++line)
                        all = ignorable[static_cast<size_t>(file)][static_cast<size_t>(line)] != 0;
                }
                chunk.ignored = all;
            }
        }

        void ApplyTolerance(std::vector<Chunk>& chunks, const std::vector<const Lines*>& files, const CompiledRules& rules)
        {
            const CompareOptions& options = rules.Options();
            auto equal = [&](int fileA, int lineA, int fileB, int lineB) {
                return EqualWithinTolerance(rules.Normalize((*files[static_cast<size_t>(fileA)])[static_cast<size_t>(lineA)]),
                    rules.Normalize((*files[static_cast<size_t>(fileB)])[static_cast<size_t>(lineB)]), options);
            };
            std::vector<Chunk> result;
            for (const Chunk& chunk : chunks)
            {
                if (!chunk.IsDifference())
                {
                    result.push_back(chunk);
                    continue;
                }
                if (files.size() == 3)
                {
                    // Three-way: the whole chunk is ignored when every changed side is within tolerance.
                    bool within = true;
                    for (int side : { 0, 2 })
                    {
                        if ((chunk.kind == ChunkKind::LeftOnly && side == 2) || (chunk.kind == ChunkKind::RightOnly && side == 0))
                            continue;
                        within = within && chunk.count[side] == chunk.count[1];
                        for (int k = 0; within && k < chunk.count[1]; ++k)
                            within = equal(side, chunk.start[side] + k, 1, chunk.start[1] + k);
                    }
                    Chunk copy = chunk;
                    copy.ignored = within;
                    result.push_back(copy);
                    continue;
                }
                if (chunk.count[0] != chunk.count[1] || chunk.count[0] == 0)
                {
                    result.push_back(chunk);
                    continue;
                }
                // Two-way, same number of lines: split into runs of within-tolerance and real changes.
                int k = 0;
                while (k < chunk.count[0])
                {
                    const bool within = equal(0, chunk.start[0] + k, 1, chunk.start[1] + k);
                    int end = k + 1;
                    while (end < chunk.count[0] && equal(0, chunk.start[0] + end, 1, chunk.start[1] + end) == within)
                        ++end;
                    Chunk part = chunk;
                    part.start[0] += k;
                    part.start[1] += k;
                    part.count[0] = part.count[1] = end - k;
                    part.ignored = within;
                    result.push_back(part);
                    k = end;
                }
            }
            chunks.swap(result);
        }
    }

    std::wstring CheckRegex(const std::wstring& pattern)
    {
        try
        {
            std::wregex test(pattern, std::regex_constants::ECMAScript);
            return std::wstring();
        }
        catch (const std::regex_error& error)
        {
            return Widen(error.what());
        }
    }

    CompiledRules::CompiledRules(const CompareOptions& options) : m_options(options)
    {
        for (const std::wstring& pattern : options.lineExpressions)
        {
            if (pattern.empty() || !CheckRegex(pattern).empty())
                continue;
            m_lineExpressions.emplace_back(pattern, std::regex_constants::ECMAScript);
        }
        for (const auto& block : options.blockExpressions)
        {
            if (block.first.empty() || block.second.empty() || !CheckRegex(block.first).empty() || !CheckRegex(block.second).empty())
                continue;
            m_blocks.emplace_back(std::wregex(block.first, std::regex_constants::ECMAScript), std::wregex(block.second, std::regex_constants::ECMAScript));
        }
        if (!options.linePairingRule.empty() && CheckRegex(options.linePairingRule).empty())
        {
            m_pairing = std::wregex(options.linePairingRule, std::regex_constants::ECMAScript);
            m_hasPairing = true;
        }
    }

    std::wstring CompiledRules::StripExpressions(const std::wstring& text) const
    {
        std::wstring result = text;
        for (const std::wregex& expression : m_lineExpressions)
            result = std::regex_replace(result, expression, L"");
        return result;
    }

    std::wstring CompiledRules::Normalize(const Line& line) const
    {
        if (m_lineExpressions.empty())
            return NormalizeText(line.text, line.eol, m_options);
        return NormalizeText(StripExpressions(line.text), line.eol, m_options);
    }

    std::vector<char> CompiledRules::IgnorableLines(const Lines& lines) const
    {
        std::vector<char> ignorable(lines.size(), 0);
        if (!m_options.ignoreBlankLines && m_lineExpressions.empty() && m_blocks.empty())
            return ignorable;
        int openBlock = -1;
        for (size_t i = 0; i < lines.size(); ++i)
        {
            const std::wstring& text = lines[i].text;
            if (openBlock >= 0)
            {
                ignorable[i] = 1;
                if (std::regex_search(text, m_blocks[static_cast<size_t>(openBlock)].second))
                    openBlock = -1;
                continue;
            }
            for (size_t b = 0; b < m_blocks.size(); ++b)
            {
                std::wsmatch begin;
                if (!std::regex_search(text, begin, m_blocks[b].first))
                    continue;
                ignorable[i] = 1;
                // The block stays open unless it also ends on this line, after the start.
                const std::wstring rest = begin.suffix().str();
                if (!std::regex_search(rest, m_blocks[b].second))
                    openBlock = static_cast<int>(b);
                break;
            }
            if (ignorable[i])
                continue;
            if (IsBlank(text))
                ignorable[i] = m_options.ignoreBlankLines ? 1 : 0;
            else if (!m_lineExpressions.empty() && IsBlank(StripExpressions(text)))
                ignorable[i] = 1;
        }
        return ignorable;
    }

    std::wstring CompiledRules::PairingKey(const std::wstring& text) const
    {
        if (!m_hasPairing)
            return std::wstring();
        std::wsmatch match;
        if (!std::regex_search(text, match, m_pairing))
            return std::wstring();
        if (match.size() > 1 && match[1].matched)
            return match[1].str();
        return match[0].str();
    }

    std::vector<int> LineKeyer::Keys(const Lines& lines)
    {
        std::vector<int> keys;
        keys.reserve(lines.size());
        for (const Line& line : lines)
        {
            auto inserted = m_ids.emplace(m_rules->Normalize(line), static_cast<int>(m_ids.size()));
            keys.push_back(inserted.first->second);
        }
        return keys;
    }

    std::wstring NormalizeLine(const Line& line, const CompareOptions& options)
    {
        return CompiledRules(options).Normalize(line);
    }

    bool EqualWithinTolerance(const std::wstring& a, const std::wstring& b, const CompareOptions& options)
    {
        size_t i = 0, j = 0;
        while (i < a.size() && j < b.size())
        {
            size_t ni = i, nj = j;
            double x = 0, y = 0;
            if (ParseNumber(a, ni, x) && ParseNumber(b, nj, y))
            {
                const double diff = std::fabs(x - y);
                const bool absoluteOk = options.absoluteTolerance > 0 && diff <= options.absoluteTolerance * (1 + 1e-12);
                const bool relativeOk = options.relativeTolerance > 0 && diff <= options.relativeTolerance / 100.0 * (std::max)(std::fabs(x), std::fabs(y)) * (1 + 1e-12);
                if (diff != 0 && !absoluteOk && !relativeOk)
                    return false;
                i = ni;
                j = nj;
                continue;
            }
            if (a[i] != b[j])
                return false;
            ++i;
            ++j;
        }
        return i == a.size() && j == b.size();
    }

    std::vector<Chunk> CompareLines(const std::vector<const Lines*>& files, const CompareOptions& options, const std::vector<SyncPoint>& syncPoints)
    {
        const int count = files.size() == 3 ? 3 : 2;
        auto rules = std::make_shared<CompiledRules>(options);
        LineKeyer keyer(rules);
        std::vector<std::vector<int>> keys;
        std::vector<std::vector<char>> ignorable;
        for (int file = 0; file < count; ++file)
        {
            keys.push_back(keyer.Keys(*files[static_cast<size_t>(file)]));
            ignorable.push_back(rules->IgnorableLines(*files[static_cast<size_t>(file)]));
        }

        // Segment boundaries: the valid synchronization points (in range, strictly increasing).
        std::vector<SyncPoint> bounds;
        SyncPoint previous;
        for (const SyncPoint& point : syncPoints)
        {
            bool valid = true;
            for (int file = 0; file < count; ++file)
            {
                const int line = point.line[file];
                valid = valid && line > previous.line[file] && line < static_cast<int>(keys[static_cast<size_t>(file)].size());
            }
            if (valid)
            {
                bounds.push_back(point);
                previous = point;
            }
        }
        SyncPoint end;
        for (int file = 0; file < count; ++file)
            end.line[file] = static_cast<int>(keys[static_cast<size_t>(file)].size());
        bounds.push_back(end);

        std::vector<Chunk> chunks;
        SyncPoint from;
        for (const SyncPoint& to : bounds)
        {
            std::vector<std::vector<int>> slices;
            for (int file = 0; file < count; ++file)
            {
                const std::vector<int>& all = keys[static_cast<size_t>(file)];
                slices.emplace_back(all.begin() + from.line[file], all.begin() + to.line[file]);
            }
            const std::vector<Chunk> part = count == 2 ? DiffKeys(slices[0], slices[1]) : Diff3Keys(slices[0], slices[1], slices[2]);
            for (Chunk chunk : part)
            {
                for (int file = 0; file < count; ++file)
                    chunk.start[file] += from.line[file];
                if (chunk.kind == ChunkKind::Equal && !chunks.empty() && chunks.back().kind == ChunkKind::Equal)
                {
                    for (int file = 0; file < count; ++file)
                        chunks.back().count[file] += chunk.count[file];
                }
                else
                {
                    chunks.push_back(chunk);
                }
            }
            from = to;
        }

        MarkIgnored(chunks, ignorable);
        if (options.HasNumericTolerance())
            ApplyTolerance(chunks, files, *rules);
        return chunks;
    }

    std::vector<Chunk> DiffLines(const Lines& a, const Lines& b, const CompareOptions& options)
    {
        return CompareLines({ &a, &b }, options);
    }

    std::vector<Chunk> Diff3Lines(const Lines& left, const Lines& base, const Lines& right, const CompareOptions& options)
    {
        return CompareLines({ &left, &base, &right }, options);
    }
}
