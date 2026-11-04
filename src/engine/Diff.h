// Diff.h - line-level comparison of two files (Myers' O(ND) algorithm, linear space).
#pragma once

#include <memory>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "TextLines.h"

namespace mf
{
    enum class WhitespaceMode
    {
        Compare,        // whitespace differences count
        IgnoreChanges,  // any run of whitespace equals any other run (leading and trailing runs ignored)
        IgnoreAll       // all whitespace is ignored
    };

    struct CompareOptions
    {
        WhitespaceMode whitespace = WhitespaceMode::Compare;
        bool ignoreCase = false;
        bool ignoreEol = true;            // CR LF, LF and CR line endings compare equal
        bool ignoreBlankLines = false;    // inserted or removed blank lines are not differences

        // Line expressions: text matching any of these regular expressions (ECMAScript syntax) is
        // left out when lines are compared, e.g. "//.*$" for C++ comments or "\$Id[^$]*\$".
        // A line that becomes empty this way is ignored like a blank line.
        std::vector<std::wstring> lineExpressions;
        // Block expressions: lines from one matching `first` up to the next matching `second`
        // (inclusive) are ignored, e.g. generated sections.
        std::vector<std::pair<std::wstring, std::wstring>> blockExpressions;
        // Numeric tolerance: numbers that differ by at most `absoluteTolerance`, or by at most
        // `relativeTolerance` percent, compare equal (0 = off).
        double absoluteTolerance = 0;
        double relativeTolerance = 0;
        // Line-pairing rule: a regular expression whose first capture group (or whole match) is a
        // key; inside a changed block, lines with the same key are shown as corresponding.
        std::wstring linePairingRule;

        bool HasNumericTolerance() const
        {
            return absoluteTolerance > 0 || relativeTolerance > 0;
        }
    };

    // Empty when pattern is a valid regular expression, otherwise the reason it is not.
    std::wstring CheckRegex(const std::wstring& pattern);

    // The options' regular expressions, compiled once per comparison. Invalid ones are skipped.
    class CompiledRules
    {
    public:
        explicit CompiledRules(const CompareOptions& options);

        const CompareOptions& Options() const
        {
            return m_options;
        }
        // The text a line is compared by.
        std::wstring Normalize(const Line& line) const;
        // Per line: 1 when the line is ignored (blank-line option, line expressions, block expressions).
        std::vector<char> IgnorableLines(const Lines& lines) const;
        // The pairing key of a line ("" when the rule is not set or does not match).
        std::wstring PairingKey(const std::wstring& text) const;
        bool HasPairingRule() const
        {
            return m_hasPairing;
        }

    private:
        std::wstring StripExpressions(const std::wstring& text) const;

        CompareOptions m_options;
        std::vector<std::wregex> m_lineExpressions;
        std::vector<std::pair<std::wregex, std::wregex>> m_blocks;
        std::wregex m_pairing;
        bool m_hasPairing = false;
    };

    // The text a line is compared by, after applying the options.
    std::wstring NormalizeLine(const Line& line, const CompareOptions& options);

    // True when two (normalized) lines are equal apart from numbers within the options' tolerance.
    bool EqualWithinTolerance(const std::wstring& a, const std::wstring& b, const CompareOptions& options);

    // Gives every distinct normalized line a small integer, shared by all files of one comparison,
    // so the diff compares integers instead of strings.
    class LineKeyer
    {
    public:
        explicit LineKeyer(const CompareOptions& options) : m_rules(std::make_shared<CompiledRules>(options))
        {
        }
        explicit LineKeyer(std::shared_ptr<const CompiledRules> rules) : m_rules(std::move(rules))
        {
        }
        std::vector<int> Keys(const Lines& lines);
        const CompiledRules& Rules() const
        {
            return *m_rules;
        }

    private:
        std::shared_ptr<const CompiledRules> m_rules;
        std::unordered_map<std::wstring, int> m_ids;
    };

    enum class ChunkKind : unsigned char
    {
        Equal,
        Change,     // two-way: the files differ here
        LeftOnly,   // three-way: only the left file changed the ancestor (middle) here
        RightOnly,  // three-way: only the right file changed it
        BothSame,   // three-way: both changed it the same way
        Conflict    // three-way: both changed it, differently
    };

    // A run of corresponding lines: lines [start[i], start[i] + count[i]) of file i. Two-way
    // comparisons use files 0 and 1; three-way ones use 0 (left), 1 (middle/ancestor) and 2 (right).
    // A comparison's chunks cover every line of every file, in order.
    struct Chunk
    {
        ChunkKind kind = ChunkKind::Equal;
        bool ignored = false;  // differs, but only in ways the options ignore
        int start[3] = {};
        int count[3] = {};

        bool IsDifference() const
        {
            return kind != ChunkKind::Equal && !ignored;
        }
        bool IsConflict() const
        {
            return kind == ChunkKind::Conflict && !ignored;
        }
        int End(int file) const
        {
            return start[file] + count[file];
        }
    };

    // Marks which elements of a and b are not part of a longest common subsequence.
    void DiffSequences(const std::vector<int>& a, const std::vector<int>& b, std::vector<char>& changedA, std::vector<char>& changedB);

    // Two-way chunks from integer keys (files 0 and 1).
    std::vector<Chunk> DiffKeys(const std::vector<int>& a, const std::vector<int>& b);

    // A user-defined alignment point: line[i] of file i must correspond (the files are compared
    // separately before and from these lines).
    struct SyncPoint
    {
        int line[3] = {};
    };

    // The full comparison of 2 or 3 files (three: left, ancestor, right): diff with the options,
    // split at the synchronization points, then ignored and within-tolerance blocks marked.
    std::vector<Chunk> CompareLines(const std::vector<const Lines*>& files, const CompareOptions& options,
        const std::vector<SyncPoint>& syncPoints = std::vector<SyncPoint>());

    // Two-way comparison of whole files.
    std::vector<Chunk> DiffLines(const Lines& a, const Lines& b, const CompareOptions& options);
}
