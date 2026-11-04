// InlineDiff.cpp - token-level diff of two lines and similarity-based line pairing.
#include "InlineDiff.h"

#include <algorithm>
#include <cwctype>
#include <unordered_map>

namespace mf
{
    namespace
    {
        struct Token
        {
            int start;
            int length;
            bool space;
        };

        bool IsSpace(wchar_t c)
        {
            return c == L' ' || c == L'\t' || c == L'\f' || c == L'\v' || c == 0x00A0 || c == 0x3000;
        }

        bool IsWordChar(wchar_t c)
        {
            return c == L'_' || std::iswalnum(c);
        }

        std::vector<Token> Tokenize(const std::wstring& text)
        {
            std::vector<Token> tokens;
            const int n = static_cast<int>(text.size());
            int i = 0;
            while (i < n)
            {
                const int start = i;
                if (IsSpace(text[i]))
                {
                    while (i < n && IsSpace(text[i]))
                        ++i;
                    tokens.push_back({ start, i - start, true });
                }
                else if (IsWordChar(text[i]))
                {
                    while (i < n && IsWordChar(text[i]))
                        ++i;
                    tokens.push_back({ start, i - start, false });
                }
                else
                {
                    ++i;
                    // Keep a surrogate pair together.
                    if (text[start] >= 0xD800 && text[start] <= 0xDBFF && i < n && text[i] >= 0xDC00 && text[i] <= 0xDFFF)
                        ++i;
                    tokens.push_back({ start, i - start, false });
                }
            }
            return tokens;
        }

        class TokenKeyer
        {
        public:
            explicit TokenKeyer(const CompareOptions& options) : m_options(options)
            {
            }

            // Keys for the tokens that take part in the comparison; kept[] gives their token indexes.
            std::vector<int> Keys(const std::wstring& text, const std::vector<Token>& tokens, std::vector<int>& kept)
            {
                std::vector<int> keys;
                kept.clear();
                for (size_t t = 0; t < tokens.size(); ++t)
                {
                    const Token& token = tokens[t];
                    std::wstring key;
                    if (token.space)
                    {
                        if (m_options.whitespace == WhitespaceMode::IgnoreAll)
                            continue;
                        if (m_options.whitespace == WhitespaceMode::IgnoreChanges)
                        {
                            // Leading and trailing whitespace are ignored; inner runs all compare equal.
                            if (t == 0 || t + 1 == tokens.size())
                                continue;
                            key = L" ";
                        }
                    }
                    if (key.empty())
                        key.assign(text, static_cast<size_t>(token.start), static_cast<size_t>(token.length));
                    if (m_options.ignoreCase)
                    {
                        for (wchar_t& c : key)
                            c = static_cast<wchar_t>(std::towlower(c));
                    }
                    auto inserted = m_ids.emplace(std::move(key), static_cast<int>(m_ids.size()));
                    keys.push_back(inserted.first->second);
                    kept.push_back(static_cast<int>(t));
                }
                return keys;
            }

        private:
            CompareOptions m_options;
            std::unordered_map<std::wstring, int> m_ids;
        };

        // Turns changed tokens into character ranges, joining neighbours (and the whitespace
        // between two changed tokens) into one range.
        std::vector<CharRange> ToRanges(const std::vector<Token>& tokens, const std::vector<int>& kept, const std::vector<char>& changed)
        {
            std::vector<CharRange> ranges;
            int lastToken = -2;
            for (size_t k = 0; k < kept.size(); ++k)
            {
                if (!changed[k])
                    continue;
                const int t = kept[k];
                const Token& token = tokens[static_cast<size_t>(t)];
                const bool joinable = !ranges.empty() && (t == lastToken + 1 || (t == lastToken + 2 && tokens[static_cast<size_t>(t - 1)].space));
                if (joinable)
                    ranges.back().length = token.start + token.length - ranges.back().start;
                else
                    ranges.push_back({ token.start, token.length });
                lastToken = t;
            }
            return ranges;
        }

        int SignificantLength(const std::vector<Token>& tokens, const std::vector<int>& kept)
        {
            int total = 0;
            for (int t : kept)
                total += tokens[static_cast<size_t>(t)].space ? 1 : tokens[static_cast<size_t>(t)].length;
            return total;
        }
    }

    InlineDiff DiffLineChars(const std::wstring& a, const std::wstring& b, const CompareOptions& options)
    {
        InlineDiff result;
        const std::vector<Token> tokensA = Tokenize(a);
        const std::vector<Token> tokensB = Tokenize(b);
        TokenKeyer keyer(options);
        std::vector<int> keptA, keptB;
        const std::vector<int> keysA = keyer.Keys(a, tokensA, keptA);
        const std::vector<int> keysB = keyer.Keys(b, tokensB, keptB);
        std::vector<char> changedA, changedB;
        DiffSequences(keysA, keysB, changedA, changedB);
        result.a = ToRanges(tokensA, keptA, changedA);
        result.b = ToRanges(tokensB, keptB, changedB);

        // Similarity: the share of (non-whitespace weighted) token text the lines have in common.
        int common = 0;
        for (size_t k = 0; k < keptA.size(); ++k)
        {
            if (!changedA[k])
            {
                const Token& token = tokensA[static_cast<size_t>(keptA[k])];
                common += token.space ? 1 : token.length;
            }
        }
        const int total = SignificantLength(tokensA, keptA) + SignificantLength(tokensB, keptB);
        result.similarity = total == 0 ? 1.0 : 2.0 * common / total;
        return result;
    }

    std::vector<std::pair<int, int>> PairLines(const std::vector<const std::wstring*>& a, const std::vector<const std::wstring*>& b, const CompareOptions& options)
    {
        std::vector<std::pair<int, int>> pairs;
        const int n = static_cast<int>(a.size());
        const int m = static_cast<int>(b.size());
        if (n == 0 || m == 0)
            return pairs;
        if (static_cast<long long>(n) * m > 2500)
        {
            // Too big to score every pair: pair lines by position.
            for (int i = 0; i < (std::min)(n, m); ++i)
                pairs.emplace_back(i, i);
            return pairs;
        }

        const double threshold = 0.4;
        // A line-pairing rule decides for lines that both have a key: same key pairs, different keys don't.
        const CompiledRules rules(options);
        std::vector<std::wstring> keysA, keysB;
        if (rules.HasPairingRule())
        {
            for (const std::wstring* line : a)
                keysA.push_back(rules.PairingKey(*line));
            for (const std::wstring* line : b)
                keysB.push_back(rules.PairingKey(*line));
        }
        std::vector<double> score(static_cast<size_t>(n * m));
        for (int i = 0; i < n; ++i)
        {
            for (int j = 0; j < m; ++j)
            {
                double& s = score[static_cast<size_t>(i * m + j)];
                if (!keysA.empty() && !keysA[static_cast<size_t>(i)].empty() && !keysB[static_cast<size_t>(j)].empty())
                    s = keysA[static_cast<size_t>(i)] == keysB[static_cast<size_t>(j)] ? 1.0 : 0.0;
                else
                    s = DiffLineChars(*a[static_cast<size_t>(i)], *b[static_cast<size_t>(j)], options).similarity;
            }
        }
        // best[i][j]: the best total similarity pairing a[i..] with b[j..], in order.
        std::vector<double> best(static_cast<size_t>((n + 1) * (m + 1)), 0.0);
        auto at = [m](int i, int j) { return static_cast<size_t>(i * (m + 1) + j); };
        for (int i = n - 1; i >= 0; --i)
        {
            for (int j = m - 1; j >= 0; --j)
            {
                double value = (std::max)(best[at(i + 1, j)], best[at(i, j + 1)]);
                const double s = score[static_cast<size_t>(i * m + j)];
                // A small bonus for pairing keeps equally good alignments in step.
                if (s >= threshold)
                    value = (std::max)(value, best[at(i + 1, j + 1)] + s + 0.001);
                best[at(i, j)] = value;
            }
        }
        int i = 0, j = 0;
        while (i < n && j < m)
        {
            const double s = score[static_cast<size_t>(i * m + j)];
            if (s >= threshold && best[at(i, j)] == best[at(i + 1, j + 1)] + s + 0.001)
            {
                pairs.emplace_back(i, j);
                ++i;
                ++j;
            }
            else if (best[at(i, j)] == best[at(i + 1, j)])
            {
                ++i;
            }
            else
            {
                ++j;
            }
        }
        return pairs;
    }
}
