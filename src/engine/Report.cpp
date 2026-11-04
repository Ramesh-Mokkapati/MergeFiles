// Report.cpp - HTML, XML and unified diff reports.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "Report.h"

#include <algorithm>
#include <cwchar>

#include "DocumentText.h"
#include "Encoding.h"
#include "InlineDiff.h"
#include "Xml.h"

namespace mf
{
    namespace
    {
        const wchar_t* kStyle = LR"(
body { font: 14px "Segoe UI", Arial, sans-serif; color: #1e1e1e; margin: 24px; background: #fff; }
h1 { font-size: 20px; margin: 0 0 4px 0; }
h2 { font-size: 16px; margin: 28px 0 6px 0; }
.meta { color: #6e6e74; font-size: 12px; margin-bottom: 14px; }
table.files { border-collapse: collapse; margin-bottom: 10px; font-size: 13px; }
table.files td { padding: 2px 12px 2px 0; }
.summary { margin: 8px 0 14px 0; font-weight: 600; }
table.cmp { border-collapse: collapse; width: 100%; table-layout: fixed; font: 13px Consolas, "Cascadia Mono", monospace; }
table.cmp th { background: #f6f6f8; border-bottom: 1px solid #d0d0d4; text-align: left; padding: 4px 6px; font: 600 13px "Segoe UI", sans-serif; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
table.cmp td { padding: 1px 6px; vertical-align: top; white-space: pre-wrap; word-break: break-all; tab-size: 4; }
td.ln { width: 4em; color: #8c8c8c; text-align: right; user-select: none; }
td.sep { width: 6px; padding: 0; background: #f4f4f6; }
.ins { background: #def4da; } .ins .d { background: #a6e29c; }
.rem { background: #fde4ce; } .rem .d { background: #f6be8c; }
.chg { background: #eeeafc; } .chg .d { background: #d0c4f6; }
.conf { background: #fcdede; } .conf .d { background: #f4aaaa; }
.ign { background: #f3f3f3; color: #777; }
.pad { background: repeating-linear-gradient(135deg, #f0f0f2, #f0f0f2 4px, #e6e6ea 4px, #e6e6ea 8px); }
tr.first td { border-top: 1px solid #b4b4c0; }
tr.gap td { background: #f6f6f8; color: #6e6e74; text-align: center; font-style: italic; padding: 3px; }
tr.note td { background: #fff8d6; color: #5a4a00; font: 12px "Segoe UI", sans-serif; padding: 3px 10px; }
.nav { margin: 8px 0; } .nav button { font: 13px "Segoe UI", sans-serif; padding: 3px 12px; margin-right: 6px; }
table.folders { border-collapse: collapse; width: 100%; font-size: 13px; }
table.folders th { background: #f6f6f8; border-bottom: 1px solid #d0d0d4; text-align: left; padding: 4px 8px; }
table.folders td { padding: 2px 8px; white-space: nowrap; }
td.num { text-align: right; }
.LeftOnly, .LeftChanged { background: #fde4ce; } .RightOnly, .RightChanged { background: #def4da; }
.Different, .BothSame { background: #eeeafc; } .Conflict, .Error { background: #fcdede; }
)";

        const wchar_t* kSlideScript = LR"(
<script>
(function () {
  var slides = document.querySelectorAll('tbody.slide'), current = 0;
  function show(i) {
    current = Math.max(0, Math.min(slides.length - 1, i));
    for (var k = 0; k < slides.length; k++) slides[k].style.display = k === current ? '' : 'none';
    document.getElementById('position').textContent = slides.length ? (current + 1) + ' of ' + slides.length : 'No changes';
  }
  window.slideMove = function (d) { show(current + d); };
  window.slideAll = function () { for (var k = 0; k < slides.length; k++) slides[k].style.display = ''; document.getElementById('position').textContent = 'All changes'; };
  document.addEventListener('keydown', function (e) {
    if (e.key === 'ArrowRight' || e.key === 'PageDown') slideMove(1);
    if (e.key === 'ArrowLeft' || e.key === 'PageUp') slideMove(-1);
  });
  show(0);
})();
</script>
)";

        std::wstring Now()
        {
            SYSTEMTIME time = {};
            GetLocalTime(&time);
            wchar_t text[64];
            swprintf_s(text, L"%04d-%02d-%02d %02d:%02d", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
            return text;
        }

        std::wstring Name(const TextBuffer& buffer)
        {
            return buffer.path.empty() ? buffer.title : buffer.path;
        }

        const wchar_t* PaneName(const Comparison& comparison, int pane)
        {
            if (comparison.IsThreeWay())
                return pane == 0 ? L"Left" : (pane == 1 ? L"Ancestor" : L"Right");
            return pane == 0 ? L"Left" : L"Right";
        }

        const wchar_t* StyleClass(ChunkStyle style)
        {
            switch (style)
            {
            case ChunkStyle::Insertion:
                return L"ins";
            case ChunkStyle::Removal:
                return L"rem";
            case ChunkStyle::Change:
                return L"chg";
            case ChunkStyle::Conflict:
                return L"conf";
            case ChunkStyle::Ignored:
                return L"ign";
            default:
                return L"";
            }
        }

        // Character-level detail of one difference chunk in one pane (as the GUI computes it).
        struct PaneDetail
        {
            std::vector<char> paired;
            std::vector<std::vector<CharRange>> ranges;
        };

        PaneDetail Detail(const Comparison& comparison, const Chunk& chunk, int pane)
        {
            PaneDetail detail;
            int partner = 1 - pane;
            if (comparison.IsThreeWay())
                partner = pane == 1 ? (chunk.kind == ChunkKind::RightOnly ? 2 : 0) : 1;
            std::vector<const std::wstring*> mine, theirs;
            for (int line = chunk.start[pane]; line < chunk.End(pane); ++line)
                mine.push_back(&comparison.Buffer(pane).LineText(line));
            for (int line = chunk.start[partner]; line < chunk.End(partner); ++line)
                theirs.push_back(&comparison.Buffer(partner).LineText(line));
            detail.paired.assign(mine.size(), 0);
            detail.ranges.resize(mine.size());
            for (const auto& pair : PairLines(mine, theirs, comparison.Options()))
            {
                detail.paired[static_cast<size_t>(pair.first)] = 1;
                detail.ranges[static_cast<size_t>(pair.first)] = DiffLineChars(*mine[static_cast<size_t>(pair.first)], *theirs[static_cast<size_t>(pair.second)], comparison.Options()).a;
            }
            return detail;
        }

        std::wstring HighlightedText(const std::wstring& text, const std::vector<CharRange>* ranges)
        {
            if (!ranges || ranges->empty())
                return XmlEscape(text);
            std::wstring out;
            int at = 0;
            for (const CharRange& range : *ranges)
            {
                const int start = (std::min)(range.start, static_cast<int>(text.size()));
                const int end = (std::min)(range.start + range.length, static_cast<int>(text.size()));
                out += XmlEscape(text.substr(static_cast<size_t>(at), static_cast<size_t>(start - at)));
                out += L"<span class=\"d\">" + XmlEscape(text.substr(static_cast<size_t>(start), static_cast<size_t>(end - start))) + L"</span>";
                at = end;
            }
            out += XmlEscape(text.substr(static_cast<size_t>(at)));
            return out;
        }

        // One display row: the same row of a chunk in every pane (shorter sides are padded).
        struct RowRef
        {
            int chunk;
            int rel;
        };

        // The side-by-side table of a comparison (without the page around it).
        std::wstring HtmlTable(const Comparison& comparison, int context, bool slideshow)
        {
            const int panes = comparison.PaneCount();
            const int reference = comparison.ReferencePane();
            const std::vector<Chunk>& chunks = comparison.Chunks();
            std::vector<RowRef> rows;
            std::vector<char> differs;
            for (size_t k = 0; k < chunks.size(); ++k)
            {
                int height = 0;
                for (int pane = 0; pane < panes; ++pane)
                    height = (std::max)(height, chunks[k].count[pane]);
                for (int r = 0; r < height; ++r)
                {
                    rows.push_back({ static_cast<int>(k), r });
                    differs.push_back(chunks[k].IsDifference() ? 1 : 0);
                }
            }
            // Which rows to show.
            if (slideshow)
                context = context < 0 ? 3 : context;
            std::vector<char> visible(rows.size(), context < 0 ? 1 : 0);
            if (context >= 0)
            {
                for (size_t i = 0; i < rows.size(); ++i)
                {
                    if (!differs[i])
                        continue;
                    const size_t from = i >= static_cast<size_t>(context) ? i - static_cast<size_t>(context) : 0;
                    const size_t to = (std::min)(rows.size() - 1, i + static_cast<size_t>(context));
                    for (size_t j = from; j <= to; ++j)
                        visible[j] = 1;
                }
            }

            std::vector<std::pair<int, PaneDetail>> details;  // cache for the current chunk
            int detailChunk = -1;
            std::wstring out = L"<table class=\"cmp\"><colgroup>";
            for (int pane = 0; pane < panes; ++pane)
                out += pane ? L"<col style=\"width:6px\"><col style=\"width:4em\"><col>" : L"<col style=\"width:4em\"><col>";
            out += L"</colgroup><thead><tr>";
            for (int pane = 0; pane < panes; ++pane)
            {
                if (pane)
                    out += L"<th class=\"sep\"></th>";
                out += L"<th colspan=\"2\">" + std::wstring(PaneName(comparison, pane)) + L": " + XmlEscape(Name(comparison.Buffer(pane))) + L"</th>";
            }
            out += L"</tr></thead>\n";
            const std::wstring columns = std::to_wstring(panes * 3 - 1);

            bool open = false;
            size_t i = 0;
            while (i < rows.size())
            {
                if (!visible[i])
                {
                    size_t j = i;
                    int hidden = 0;
                    while (j < rows.size() && !visible[j])
                    {
                        hidden += rows[j].rel < chunks[static_cast<size_t>(rows[j].chunk)].count[0] ? 1 : 0;
                        ++j;
                    }
                    if (open)
                    {
                        out += L"</tbody>\n";
                        open = false;
                    }
                    if (!slideshow)
                        out += L"<tbody><tr class=\"gap\"><td colspan=\"" + columns + L"\">\x22EF " + std::to_wstring(hidden) + L" unchanged line" +
                            (hidden == 1 ? L"" : L"s") + L"</td></tr></tbody>\n";
                    i = j;
                    continue;
                }
                if (!open)
                {
                    out += slideshow ? L"<tbody class=\"slide\">\n" : L"<tbody>\n";
                    open = true;
                }
                const RowRef row = rows[i];
                const Chunk& chunk = chunks[static_cast<size_t>(row.chunk)];
                if (chunk.IsDifference() && row.chunk != detailChunk)
                {
                    detailChunk = row.chunk;
                    details.clear();
                    for (int pane = 0; pane < panes; ++pane)
                        details.emplace_back(pane, Detail(comparison, chunk, pane));
                }
                const bool firstOfBlock = row.rel == 0 && chunk.IsDifference();
                out += firstOfBlock ? L"<tr class=\"first\">" : L"<tr>";
                std::wstring notes;
                for (int pane = 0; pane < panes; ++pane)
                {
                    if (pane)
                        out += L"<td class=\"sep\"></td>";
                    if (row.rel >= chunk.count[pane])
                    {
                        out += L"<td class=\"ln pad\"></td><td class=\"t pad\"></td>";
                        continue;
                    }
                    const int line = chunk.start[pane] + row.rel;
                    const Line& data = comparison.Buffer(pane).GetLine(line);
                    ChunkStyle style = comparison.StyleInPane(chunk, pane);
                    const std::vector<CharRange>* ranges = nullptr;
                    if (chunk.IsDifference() && (style == ChunkStyle::Change || style == ChunkStyle::Conflict))
                    {
                        const PaneDetail& detail = details[static_cast<size_t>(pane)].second;
                        if (detail.paired[static_cast<size_t>(row.rel)])
                            ranges = &detail.ranges[static_cast<size_t>(row.rel)];
                        else if (style == ChunkStyle::Change)
                            style = pane == reference ? ChunkStyle::Removal : ChunkStyle::Insertion;
                    }
                    const std::wstring cls = StyleClass(style);
                    out += L"<td class=\"ln " + cls + L"\">" + std::to_wstring(line + 1) + L"</td><td class=\"t " + cls + L"\">" + HighlightedText(data.text, ranges) + L"</td>";
                    if ((data.flags & LineBookmark) && !data.comment.empty())
                        notes += L"\x270E " + std::wstring(PaneName(comparison, pane)) + L" line " + std::to_wstring(line + 1) + L": " + XmlEscape(data.comment) + L"<br>";
                }
                out += L"</tr>\n";
                if (!notes.empty())
                    out += L"<tr class=\"note\"><td colspan=\"" + columns + L"\">" + notes + L"</td></tr>\n";
                ++i;
            }
            if (open)
                out += L"</tbody>\n";
            out += L"</table>\n";
            return out;
        }

        std::wstring Summary(const Comparison& comparison)
        {
            const ComparisonStats stats = comparison.Stats();
            if (stats.Differences() == 0)
                return stats.ignored ? L"No differences (" + std::to_wstring(stats.ignored) + L" ignored)" : L"No differences";
            std::wstring text = std::to_wstring(stats.removals) + L" removal(s) \x2022 " + std::to_wstring(stats.insertions) + L" insertion(s) \x2022 " +
                std::to_wstring(stats.changes) + L" change(s)";
            if (comparison.IsThreeWay())
                text += L" \x2022 " + std::to_wstring(stats.conflicts) + L" conflict(s)";
            if (stats.ignored)
                text += L" \x2022 " + std::to_wstring(stats.ignored) + L" ignored";
            return text;
        }

        std::wstring FilesTable(const Comparison& comparison)
        {
            std::wstring out = L"<table class=\"files\">";
            for (int pane = 0; pane < comparison.PaneCount(); ++pane)
            {
                const TextBuffer& buffer = comparison.Buffer(pane);
                out += L"<tr><td><b>" + std::wstring(PaneName(comparison, pane)) + L"</b></td><td>" + XmlEscape(Name(buffer)) + L"</td><td>" +
                    XmlEscape(EncodingName(buffer.encoding)) + L"</td><td>" + std::to_wstring(buffer.LineCount()) + L" lines</td></tr>";
            }
            return out + L"</table>\n";
        }

        std::wstring DefaultTitle(const Comparison& comparison)
        {
            std::wstring title;
            for (int pane = 0; pane < comparison.PaneCount(); ++pane)
                title += (pane ? L" \x2194 " : L"") + comparison.Buffer(pane).title;
            return title;
        }

        std::wstring HtmlReport(const Comparison& comparison, const ReportOptions& options)
        {
            const bool slideshow = options.format == ReportFormat::HtmlSlideshow;
            const std::wstring title = options.title.empty() ? DefaultTitle(comparison) : options.title;
            std::wstring out = L"<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>" + XmlEscape(title) + L"</title><style>" + kStyle + L"</style></head><body>\n";
            out += L"<h1>" + XmlEscape(title) + L"</h1>\n<div class=\"meta\">File comparison report by MergeFiles, " + Now() + L"</div>\n";
            out += FilesTable(comparison);
            out += L"<div class=\"summary\">" + Summary(comparison) + L"</div>\n";
            if (slideshow)
                out += L"<div class=\"nav\"><button onclick=\"slideMove(-1)\">\x2190 Previous</button><button onclick=\"slideMove(1)\">Next \x2192</button>"
                       L"<button onclick=\"slideAll()\">Show all</button> <span id=\"position\"></span></div>\n";
            out += HtmlTable(comparison, options.context, slideshow);
            if (slideshow)
                out += kSlideScript;
            out += L"</body></html>\n";
            return out;
        }

        const wchar_t* KindName(const Chunk& chunk)
        {
            switch (chunk.kind)
            {
            case ChunkKind::Equal:
                return L"equal";
            case ChunkKind::LeftOnly:
                return L"left";
            case ChunkKind::RightOnly:
                return L"right";
            case ChunkKind::BothSame:
                return L"both";
            case ChunkKind::Conflict:
                return L"conflict";
            default:
                return L"change";
            }
        }

        std::wstring XmlReport(const Comparison& comparison)
        {
            std::wstring out = L"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<comparison generator=\"MergeFiles\" date=\"" + Now() + L"\" panes=\"" +
                std::to_wstring(comparison.PaneCount()) + L"\">\n";
            for (int pane = 0; pane < comparison.PaneCount(); ++pane)
            {
                const TextBuffer& buffer = comparison.Buffer(pane);
                out += L"  <file pane=\"" + std::to_wstring(pane) + L"\" role=\"" + PaneName(comparison, pane) + L"\" path=\"" + XmlEscape(Name(buffer), true) +
                    L"\" encoding=\"" + XmlEscape(EncodingName(buffer.encoding), true) + L"\" lines=\"" + std::to_wstring(buffer.LineCount()) + L"\"/>\n";
            }
            const ComparisonStats stats = comparison.Stats();
            out += L"  <summary removals=\"" + std::to_wstring(stats.removals) + L"\" insertions=\"" + std::to_wstring(stats.insertions) + L"\" changes=\"" +
                std::to_wstring(stats.changes) + L"\" conflicts=\"" + std::to_wstring(stats.conflicts) + L"\" ignored=\"" + std::to_wstring(stats.ignored) + L"\"/>\n";
            for (const Chunk& chunk : comparison.Chunks())
            {
                out += L"  <block kind=\"" + std::wstring(KindName(chunk)) + L"\"" + (chunk.ignored ? L" ignored=\"true\"" : L"") + L">\n";
                for (int pane = 0; pane < comparison.PaneCount(); ++pane)
                {
                    out += L"    <pane index=\"" + std::to_wstring(pane) + L"\" start=\"" + std::to_wstring(chunk.start[pane] + 1) + L"\" count=\"" + std::to_wstring(chunk.count[pane]) + L"\"";
                    if (chunk.kind == ChunkKind::Equal || chunk.count[pane] == 0)
                    {
                        out += L"/>\n";
                        continue;
                    }
                    out += L">\n";
                    for (int line = chunk.start[pane]; line < chunk.End(pane); ++line)
                        out += L"      <line number=\"" + std::to_wstring(line + 1) + L"\">" + XmlEscape(comparison.Buffer(pane).LineText(line)) + L"</line>\n";
                    out += L"    </pane>\n";
                }
                out += L"  </block>\n";
            }
            for (int pane = 0; pane < comparison.PaneCount(); ++pane)
            {
                const TextBuffer& buffer = comparison.Buffer(pane);
                for (int line = 0; line < buffer.LineCount(); ++line)
                {
                    const Line& data = buffer.GetLine(line);
                    if (data.flags & LineBookmark)
                        out += L"  <bookmark pane=\"" + std::to_wstring(pane) + L"\" line=\"" + std::to_wstring(line + 1) + L"\">" + XmlEscape(data.comment) + L"</bookmark>\n";
                }
            }
            return out + L"</comparison>\n";
        }

        void AppendUnified(std::wstring& out, const Lines& a, const Lines& b, const std::vector<Chunk>& chunks, const std::wstring& nameA, const std::wstring& nameB, int context)
        {
            out += L"--- " + nameA + L"\n+++ " + nameB + L"\n";
            auto real = [](const Lines& lines, int end) { return end == static_cast<int>(lines.size()) && lines.size() > 1 && lines.back().text.empty() ? end - 1 : end; };
            auto print = [&](wchar_t prefix, const Line& line) {
                out.push_back(prefix);
                out += line.text;
                out += L"\n";
                if (line.eol == Eol::None)
                    out += L"\\ No newline at end of file\n";
            };
            size_t i = 0;
            while (i < chunks.size())
            {
                if (!chunks[i].IsDifference())
                {
                    ++i;
                    continue;
                }
                size_t last = i;
                while (last + 2 < chunks.size() && chunks[last + 1].count[0] <= 2 * context && chunks[last + 2].IsDifference())
                    last += 2;
                const int a0 = (std::max)(0, chunks[i].start[0] - context);
                const int b0 = (std::max)(0, chunks[i].start[1] - context);
                const int a1 = real(a, (std::min)(static_cast<int>(a.size()), chunks[last].End(0) + context));
                const int b1 = real(b, (std::min)(static_cast<int>(b.size()), chunks[last].End(1) + context));
                out += L"@@ -" + std::to_wstring(a0 + 1) + L"," + std::to_wstring(a1 - a0) + L" +" + std::to_wstring(b0 + 1) + L"," + std::to_wstring(b1 - b0) + L" @@\n";
                for (int line = a0; line < chunks[i].start[0]; ++line)
                    print(L' ', a[static_cast<size_t>(line)]);
                for (size_t k = i; k <= last; ++k)
                {
                    const Chunk& chunk = chunks[k];
                    if (chunk.IsDifference())
                    {
                        for (int line = chunk.start[0]; line < (std::min)(chunk.End(0), a1); ++line)
                            print(L'-', a[static_cast<size_t>(line)]);
                        for (int line = chunk.start[1]; line < (std::min)(chunk.End(1), b1); ++line)
                            print(L'+', b[static_cast<size_t>(line)]);
                    }
                    else
                    {
                        for (int line = chunk.start[0]; line < chunk.End(0); ++line)
                            print(L' ', a[static_cast<size_t>(line)]);
                    }
                }
                for (int line = chunks[last].End(0); line < a1; ++line)
                    print(L' ', a[static_cast<size_t>(line)]);
                i = last + 1;
            }
        }

        std::wstring Leaf(const std::wstring& path)
        {
            const size_t slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? path : path.substr(slash + 1);
        }
    }

    std::wstring UnifiedDiffText(const Comparison& comparison, int context)
    {
        context = context < 0 ? 3 : context;
        std::wstring out;
        auto pair = [&](int a, int b) {
            const Lines& linesA = comparison.Buffer(a).GetLines();
            const Lines& linesB = comparison.Buffer(b).GetLines();
            AppendUnified(out, linesA, linesB, CompareLines({ &linesA, &linesB }, comparison.Options()), Name(comparison.Buffer(a)), Name(comparison.Buffer(b)), context);
        };
        if (comparison.IsThreeWay())
        {
            pair(1, 0);
            pair(1, 2);
        }
        else
        {
            pair(0, 1);
        }
        return out;
    }

    std::wstring MakeReport(const Comparison& comparison, const ReportOptions& options)
    {
        switch (options.format)
        {
        case ReportFormat::Xml:
            return XmlReport(comparison);
        case ReportFormat::UnifiedDiff:
            return UnifiedDiffText(comparison, options.context);
        default:
            return HtmlReport(comparison, options);
        }
    }

    namespace
    {
        std::wstring Time(int64_t ticks)
        {
            if (ticks <= 0)
                return L"";
            FILETIME utc = { static_cast<DWORD>(ticks & 0xFFFFFFFF), static_cast<DWORD>(static_cast<uint64_t>(ticks) >> 32) };
            FILETIME local = {};
            SYSTEMTIME time = {};
            if (!FileTimeToLocalFileTime(&utc, &local) || !FileTimeToSystemTime(&local, &time))
                return L"";
            wchar_t text[64];
            swprintf_s(text, L"%04d-%02d-%02d %02d:%02d", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
            return text;
        }

        const wchar_t* StatusKey(ItemStatus status)
        {
            switch (status)
            {
            case ItemStatus::Identical:
                return L"Identical";
            case ItemStatus::Different:
                return L"Different";
            case ItemStatus::LeftOnly:
                return L"LeftOnly";
            case ItemStatus::RightOnly:
                return L"RightOnly";
            case ItemStatus::LeftChanged:
                return L"LeftChanged";
            case ItemStatus::RightChanged:
                return L"RightChanged";
            case ItemStatus::BothSame:
                return L"BothSame";
            case ItemStatus::Conflict:
                return L"Conflict";
            case ItemStatus::Error:
                return L"Error";
            default:
                return L"Pending";
            }
        }

        // The tree in display order (folders first), skipping the root.
        void Order(const FolderComparison& comparison, int item, std::vector<int>& order)
        {
            if (item > 0)
                order.push_back(item);
            for (int child : comparison.Item(item).children)
                Order(comparison, child, order);
        }

        int Depth(const FolderComparison& comparison, int item)
        {
            int depth = 0;
            for (int parent = comparison.Item(item).parent; parent > 0; parent = comparison.Item(parent).parent)
                ++depth;
            return depth;
        }

        // A file comparison for a folder item's text files, or false (binary, too large, unreadable).
        bool LoadItemComparison(const FolderComparison& folders, int item, Comparison& comparison)
        {
            const FolderItem& entry = folders.Item(item);
            for (int side = 0; side < folders.SideCount(); ++side)
            {
                if (entry.side[side].exists && entry.side[side].size > 4ull * 1024 * 1024)
                    return false;
            }
            comparison.SetOptions(folders.Options().text);
            for (int side = 0; side < folders.SideCount(); ++side)
            {
                if (!entry.side[side].exists)
                {
                    comparison.SetText(side, L"", entry.name + L" (missing)");
                    continue;
                }
                // Read through the folder comparison, so files inside archives work too.
                std::string bytes;
                std::wstring error, text;
                if (!folders.ReadItem(item, side, bytes, error))
                    return false;
                if (IsDocumentFileName(entry.name))
                {
                    if (!ExtractDocumentText(bytes, entry.name, text, error))
                        return false;
                }
                else if (LooksBinary(bytes))
                    return false;
                else
                {
                    const DecodedText decoded = DecodeText(bytes);
                    text = decoded.text;
                    comparison.SetEncoding(side, decoded.encoding);
                }
                comparison.SetText(side, text, folders.FullPath(item, side));
            }
            return true;
        }
    }

    std::wstring MakeFolderReport(const FolderComparison& comparison, const FolderReportOptions& options)
    {
        std::vector<int> order;
        if (!comparison.Items().empty())
            Order(comparison, 0, order);
        const int sides = comparison.SideCount();
        static const wchar_t* twoWay[] = { L"Left", L"Right" };
        static const wchar_t* threeWay[] = { L"Left", L"Ancestor", L"Right" };
        auto sideName = [&](int side) { return sides == 3 ? threeWay[side] : twoWay[side]; };
        auto shown = [&](const FolderItem& item) { return !options.onlyDifferences || item.status != ItemStatus::Identical; };

        if (options.xml)
        {
            std::wstring out = L"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<folderComparison generator=\"MergeFiles\" date=\"" + Now() + L"\" sides=\"" + std::to_wstring(sides) + L"\">\n";
            for (int side = 0; side < sides; ++side)
                out += L"  <root side=\"" + std::to_wstring(side) + L"\" role=\"" + sideName(side) + L"\" path=\"" + XmlEscape(comparison.Root(side), true) + L"\"/>\n";
            for (int item : order)
            {
                const FolderItem& entry = comparison.Item(item);
                if (!shown(entry))
                    continue;
                out += L"  <item path=\"" + XmlEscape(entry.relPath, true) + L"\" type=\"" + (entry.isDir ? L"folder" : L"file") + L"\" status=\"" + StatusKey(entry.status) + L"\"";
                if (!entry.note.empty())
                    out += L" note=\"" + XmlEscape(entry.note, true) + L"\"";
                out += L">\n";
                for (int side = 0; side < sides; ++side)
                {
                    const FolderSide& info = entry.side[side];
                    out += L"    <side index=\"" + std::to_wstring(side) + L"\" exists=\"" + (info.exists ? L"true" : L"false") + L"\"";
                    if (info.exists)
                        out += L" size=\"" + std::to_wstring(info.size) + L"\" modified=\"" + Time(info.modified) + L"\"";
                    out += L"/>\n";
                }
                out += L"  </item>\n";
            }
            return out + L"</folderComparison>\n";
        }

        const std::wstring title = options.title.empty() ? L"Folder comparison: " + Leaf(comparison.Root(0)) + L" \x2194 " + Leaf(comparison.Root(sides - 1)) : options.title;
        std::wstring out = L"<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>" + XmlEscape(title) + L"</title><style>" + kStyle + L"</style></head><body>\n";
        out += L"<h1>" + XmlEscape(title) + L"</h1>\n<div class=\"meta\">Folder comparison report by MergeFiles, " + Now() + L"</div>\n<table class=\"files\">";
        for (int side = 0; side < sides; ++side)
            out += L"<tr><td><b>" + std::wstring(sideName(side)) + L"</b></td><td>" + XmlEscape(comparison.Root(side)) + L"</td></tr>";
        out += L"</table>\n";
        const FolderComparison::Counts counts = comparison.CountFiles();
        if (sides == 3)
            out += L"<div class=\"summary\">" + std::to_wstring(counts.identical) + L" unchanged \x2022 " + std::to_wstring(counts.leftChanged) + L" changed on the left \x2022 " +
                std::to_wstring(counts.rightChanged) + L" changed on the right \x2022 " + std::to_wstring(counts.bothSame) + L" same on both \x2022 " +
                std::to_wstring(counts.conflicts) + L" conflicts</div>\n";
        else
            out += L"<div class=\"summary\">" + std::to_wstring(counts.identical) + L" identical \x2022 " + std::to_wstring(counts.different) + L" different \x2022 " +
                std::to_wstring(counts.leftOnly) + L" left only \x2022 " + std::to_wstring(counts.rightOnly) + L" right only</div>\n";

        out += L"<table class=\"folders\"><tr><th>Name</th><th>Status</th>";
        for (int side = 0; side < sides; ++side)
            out += L"<th>" + std::wstring(sideName(side)) + L" size</th><th>" + sideName(side) + L" modified</th>";
        out += L"</tr>\n";
        std::vector<int> fileReports;
        for (int item : order)
        {
            const FolderItem& entry = comparison.Item(item);
            if (!shown(entry))
                continue;
            std::wstring name = XmlEscape(entry.name) + (entry.isDir ? L"\\" : L"");
            const bool report = options.includeFileReports && !entry.isDir && entry.status != ItemStatus::Identical && entry.status != ItemStatus::Error;
            if (report)
            {
                name = L"<a href=\"#f" + std::to_wstring(item) + L"\">" + name + L"</a>";
                fileReports.push_back(item);
            }
            out += L"<tr class=\"" + std::wstring(StatusKey(entry.status)) + L"\"><td style=\"padding-left:" + std::to_wstring(8 + Depth(comparison, item) * 18) + L"px\">" + name +
                L"</td><td>" + StatusName(entry.status) + (entry.note.empty() ? L"" : L" \x2014 " + XmlEscape(entry.note)) + L"</td>";
            for (int side = 0; side < sides; ++side)
            {
                const FolderSide& info = entry.side[side];
                out += info.exists ? L"<td class=\"num\">" + (info.isDir ? std::wstring() : std::to_wstring(info.size)) + L"</td><td>" + Time(info.modified) + L"</td>"
                                   : std::wstring(L"<td class=\"num\">\x2014</td><td></td>");
            }
            out += L"</tr>\n";
        }
        out += L"</table>\n";

        for (int item : fileReports)
        {
            Comparison file(sides);
            out += L"<h2 id=\"f" + std::to_wstring(item) + L"\">" + XmlEscape(comparison.Item(item).relPath) + L"</h2>\n";
            if (!LoadItemComparison(comparison, item, file))
            {
                out += L"<div class=\"meta\">Binary, too large or unreadable: not shown.</div>\n";
                continue;
            }
            out += L"<div class=\"summary\">" + Summary(file) + L"</div>\n" + HtmlTable(file, options.context, false);
        }
        return out + L"</body></html>\n";
    }
}
