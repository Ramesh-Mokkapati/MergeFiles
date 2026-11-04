// main_cli.cpp - MergeFilesCli: the comparison engine of MergeFiles, for scripts and verification.
//
//   MergeFilesCli diff  <file1> <file2> [options]            unified diff; exit 0 same, 1 different
//   MergeFilesCli diff3 <left> <ancestor> <right> [options]  three-way chunk list; exit 1 if conflicts
//   MergeFilesCli merge <left> <ancestor> <right> [-o <out>] [--markers] [options]
//                       automatic merge into <out> (default: stdout), in the ancestor's encoding;
//                       exit 0 clean, 1 conflicts (kept from the ancestor, or as markers)
//   MergeFilesCli dircmp <left> <right> [folder options]    folder comparison; exit 0 same, 1 different
//   MergeFilesCli dircmp3 <left> <ancestor> <right> [...]     three-way folder comparison; exit 1 if conflicts
//   MergeFilesCli dirmerge <left> <ancestor> <right> [--dry-run] [...]
//                       automatic three-way folder merge into <ancestor>; exit 1 if conflicts remain
//   MergeFilesCli bincmp <file1> <file2>                      byte comparison; exit 0 same, 1 different
//   MergeFilesCli imgcmp <image1> <image2> [--tolerance N] [--diff-image <out.png>]
//                       pixel comparison; exit 0 same, 1 different
//   MergeFilesCli patch <patch-file> <folder> [-p N] [--dry-run] [--reverse]
//                       applies a unified diff to the files under <folder>; exit 1 if a hunk failed
//   MergeFilesCli xmlfmt <file> [-o <out>]                     one element per line, indented
//   MergeFilesCli report <left> <right> [<third>] [--format html|slideshow|xml|diff] [--context N] [-o <out>]
//                       comparison report (three files: left, ancestor, right)
//   MergeFilesCli dirreport <left> <right> [<third>] [--xml] [--files] [--all] [-o <out>]
//                       folder comparison report; --files adds a report of each changed text file
//   MergeFilesCli doctext <document> [-o <out>]               the text of a Word/Excel/PowerPoint/OpenDocument/RTF file
//   MergeFilesCli info  <file>...                             encoding, line endings, line count
//
// diff, diff3 and merge compare Word, Excel, PowerPoint, OpenDocument and RTF files by their text;
// dircmp, dircmp3 and dirreport accept .zip/.tar/.tar.gz archives in place of folders.
//   MergeFilesCli roundtrip <file>...                         decode + encode reproduces every byte?
//   MergeFilesCli --version
//
// Options: --ignore-whitespace (runs of whitespace equal), --ignore-all-whitespace, --ignore-case,
//          --strict-eol (line ending differences count), --ignore-blank-lines,
//          --line-expr <regex> (text to leave out; repeatable), --block <begin> <end> (lines to ignore;
//          repeatable), --tolerance <absolute>, --rel-tolerance <percent>
// Folder options: --mode quick|binary|thorough (default binary), --include <patterns>,
//          --exclude <patterns> (';'-separated wildcards; a trailing '\' means folders), --all
//          (list identical items too), --no-recycle-bin (dirmerge deletes permanently)
// Exit codes: 0/1 as above, 2 error.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <fcntl.h>
#include <io.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "Comparison.h"
#include "Diff3.h"
#include "DocumentText.h"
#include "Encoding.h"
#include "BinaryDiff.h"
#include "FolderCompare.h"
#include "ImageDiff.h"
#include "Packed.h"
#include "Patch.h"
#include "Report.h"
#include "XmlFormat.h"
#include "Version.h"

using namespace mf;

namespace
{
    struct Arguments
    {
        std::vector<std::wstring> files;
        std::wstring output;
        bool markers = false;
        bool dryRun = false;
        bool listAll = false;
        bool reverse = false;
        int strip = 1;
        std::wstring diffImage;
        ReportFormat reportFormat = ReportFormat::Html;
        int context = -1;
        bool xmlReport = false;
        bool fileReports = false;
        CompareOptions options;
        FolderOptions folders;
    };

    int Usage()
    {
        fwprintf(stderr,
            L"MergeFilesCli %hs - compare and merge text files\n\n"
            L"  MergeFilesCli diff  <file1> <file2> [options]\n"
            L"  MergeFilesCli diff3 <left> <ancestor> <right> [options]\n"
            L"  MergeFilesCli merge <left> <ancestor> <right> [-o <out>] [--markers] [options]\n"
            L"  MergeFilesCli dircmp <left> <right> [folder options]\n"
            L"  MergeFilesCli dircmp3 <left> <ancestor> <right> [folder options]\n"
            L"  MergeFilesCli dirmerge <left> <ancestor> <right> [--dry-run] [folder options]\n"
            L"  MergeFilesCli bincmp <file1> <file2>\n"
            L"  MergeFilesCli imgcmp <image1> <image2> [--tolerance N] [--diff-image <out.png>]\n"
            L"  MergeFilesCli patch <patch-file> <folder> [-p N] [--dry-run] [--reverse]\n"
            L"  MergeFilesCli xmlfmt <file> [-o <out>]\n"
            L"  MergeFilesCli report <left> <right> [<third>] [--format html|slideshow|xml|diff] [--context N] [-o <out>]\n"
            L"  MergeFilesCli dirreport <left> <right> [<third>] [--xml] [--files] [--all] [-o <out>]\n"
            L"  MergeFilesCli doctext <document> [-o <out>]\n"
            L"  MergeFilesCli info  <file>...\n"
            L"  MergeFilesCli roundtrip <file>...\n\n"
            L"Options: --ignore-whitespace  --ignore-all-whitespace  --ignore-case  --strict-eol\n"
            L"         --ignore-blank-lines  --line-expr <regex>  --block <begin-regex> <end-regex>\n"
            L"         --tolerance <absolute>  --rel-tolerance <percent>\n"
            L"Folder options: --mode quick|binary|thorough  --include <patterns>  --exclude <patterns>  --all\n",
            MERGEFILES_VERSION_FULL);
        return 2;
    }

    bool Load(const std::wstring& path, Lines& lines, TextEncoding& encoding)
    {
        std::string bytes;
        std::wstring error;
        if (IsDocumentFileName(path))
        {
            // Documents are compared by their text.
            std::wstring text;
            if (!ReadDocumentText(path, text, error))
            {
                fwprintf(stderr, L"%ls: %ls\n", path.c_str(), error.c_str());
                return false;
            }
            lines = SplitLines(text);
            encoding = TextEncoding();
            return true;
        }
        if (!ReadFileBytes(path, bytes, error))
        {
            fwprintf(stderr, L"%ls: %ls\n", path.c_str(), error.c_str());
            return false;
        }
        const DecodedText decoded = DecodeText(bytes);
        lines = SplitLines(decoded.text);
        encoding = decoded.encoding;
        return true;
    }

    void PrintLine(wchar_t prefix, const Line& line)
    {
        fwprintf(stdout, L"%lc%ls\n", prefix, line.text.c_str());
        if (line.eol == Eol::None)
            fwprintf(stdout, L"\\ No newline at end of file\n");
    }

    // Unified diff with 3 lines of context; hunks closer than 6 lines are joined.
    void PrintUnified(const Lines& a, const Lines& b, const std::vector<Chunk>& chunks, const std::wstring& nameA, const std::wstring& nameB)
    {
        fwprintf(stdout, L"--- %ls\n+++ %ls\n", nameA.c_str(), nameB.c_str());
        const int context = 3;
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
            const int a1 = (std::min)(static_cast<int>(a.size()), chunks[last].End(0) + context);
            const int b1 = (std::min)(static_cast<int>(b.size()), chunks[last].End(1) + context);
            // The trailing empty "line" after a final line break is not a real line in diff output.
            auto real = [](const Lines& lines, int end) { return end == static_cast<int>(lines.size()) && lines.back().text.empty() && lines.size() > 1 ? end - 1 : end; };
            const int ra1 = real(a, a1), rb1 = real(b, b1);
            fwprintf(stdout, L"@@ -%d,%d +%d,%d @@\n", a0 + 1, ra1 - a0, b0 + 1, rb1 - b0);
            for (int line = a0; line < chunks[i].start[0]; ++line)
                PrintLine(L' ', a[static_cast<size_t>(line)]);
            for (size_t k = i; k <= last; ++k)
            {
                const Chunk& chunk = chunks[k];
                if (chunk.IsDifference())
                {
                    for (int line = chunk.start[0]; line < (std::min)(chunk.End(0), ra1); ++line)
                        PrintLine(L'-', a[static_cast<size_t>(line)]);
                    for (int line = chunk.start[1]; line < (std::min)(chunk.End(1), rb1); ++line)
                        PrintLine(L'+', b[static_cast<size_t>(line)]);
                }
                else
                {
                    for (int line = chunk.start[0]; line < chunk.End(0); ++line)
                        PrintLine(L' ', a[static_cast<size_t>(line)]);
                }
            }
            for (int line = chunks[last].End(0); line < ra1; ++line)
                PrintLine(L' ', a[static_cast<size_t>(line)]);
            i = last + 1;
        }
    }

    const wchar_t* KindName(ChunkKind kind)
    {
        switch (kind)
        {
        case ChunkKind::LeftOnly:
            return L"left";
        case ChunkKind::RightOnly:
            return L"right";
        case ChunkKind::BothSame:
            return L"both";
        case ChunkKind::Conflict:
            return L"CONFLICT";
        case ChunkKind::Change:
            return L"change";
        default:
            return L"equal";
        }
    }

    int RunDiff(const Arguments& args)
    {
        if (args.files.size() != 2)
            return Usage();
        Lines a, b;
        TextEncoding ea, eb;
        if (!Load(args.files[0], a, ea) || !Load(args.files[1], b, eb))
            return 2;
        const std::vector<Chunk> chunks = DiffLines(a, b, args.options);
        bool different = false;
        for (const Chunk& chunk : chunks)
            different = different || chunk.IsDifference();
        // Ignored blocks are not differences; print only the real ones.
        std::vector<Chunk> shown = chunks;
        for (Chunk& chunk : shown)
        {
            if (chunk.ignored)
                chunk.kind = ChunkKind::Equal;
        }
        if (different)
            PrintUnified(a, b, shown, args.files[0], args.files[1]);
        return different ? 1 : 0;
    }

    int RunDiff3(const Arguments& args)
    {
        if (args.files.size() != 3)
            return Usage();
        Lines files[3];
        TextEncoding encodings[3];
        for (int i = 0; i < 3; ++i)
        {
            if (!Load(args.files[static_cast<size_t>(i)], files[i], encodings[i]))
                return 2;
        }
        const std::vector<Chunk> chunks = Diff3Lines(files[0], files[1], files[2], args.options);
        int conflicts = 0;
        for (const Chunk& chunk : chunks)
        {
            if (!chunk.IsDifference())
                continue;
            conflicts += chunk.IsConflict() ? 1 : 0;
            fwprintf(stdout, L"%-8ls left %d,%d  ancestor %d,%d  right %d,%d\n", KindName(chunk.kind),
                chunk.start[0] + 1, chunk.count[0], chunk.start[1] + 1, chunk.count[1], chunk.start[2] + 1, chunk.count[2]);
        }
        fwprintf(stdout, L"%d conflict(s)\n", conflicts);
        return conflicts ? 1 : 0;
    }

    int RunMerge(const Arguments& args)
    {
        if (args.files.size() != 3)
            return Usage();
        Lines files[3];
        TextEncoding encodings[3];
        for (int i = 0; i < 3; ++i)
        {
            if (!Load(args.files[static_cast<size_t>(i)], files[i], encodings[i]))
                return 2;
        }
        const std::vector<Chunk> chunks = Diff3Lines(files[0], files[1], files[2], args.options);
        const MergeResult merged = Merge3(files[0], files[1], files[2], chunks, args.markers);
        const std::string bytes = EncodeText(JoinLines(merged.lines), encodings[1]);
        if (args.output.empty())
        {
            // Raw bytes in the file's own encoding (stdout is in UTF-8 text mode for messages).
            fflush(stdout);
            DWORD written = 0;
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
        }
        else
        {
            std::wstring error;
            if (!WriteFileBytes(args.output, bytes, error))
            {
                fwprintf(stderr, L"%ls: %ls\n", args.output.c_str(), error.c_str());
                return 2;
            }
        }
        fwprintf(stderr, L"%d change(s) merged, %d conflict(s)\n", merged.merged, merged.conflicts);
        return merged.conflicts ? 1 : 0;
    }

    int RunFolders(const Arguments& args, bool threeWay, bool merge)
    {
        if (args.files.size() != (threeWay ? 3u : 2u))
            return Usage();
        for (const std::wstring& root : args.files)
        {
            const DWORD attributes = GetFileAttributesW(root.c_str());
            const bool folder = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
            const bool archive = attributes != INVALID_FILE_ATTRIBUTES && !folder && IsPackedArchiveName(root);
            if (!folder && !archive)
            {
                fwprintf(stderr, L"%ls: not a folder or archive\n", root.c_str());
                return 2;
            }
        }
        FolderOptions options = args.folders;
        options.text = args.options;
        FolderComparison comparison(args.files, options);
        comparison.Run();
        if (merge)
        {
            const FolderComparison::MergeReport report = comparison.AutoMerge(args.dryRun);
            for (const std::wstring& path : report.conflicts)
                fwprintf(stdout, L"CONFLICT  %ls\n", path.c_str());
            for (const std::wstring& error : report.errors)
                fwprintf(stderr, L"%ls\n", error.c_str());
            fwprintf(stdout, L"%ls%d item(s) taken from one side, %d file(s) merged line by line, %d conflict(s)\n",
                args.dryRun ? L"(dry run) " : L"", report.applied, report.textMerged, static_cast<int>(report.conflicts.size()));
            return !report.errors.empty() ? 2 : (report.conflicts.empty() ? 0 : 1);
        }
        for (const FolderItem& item : comparison.Items())
        {
            if (item.parent < 0 || (item.isDir && item.status != ItemStatus::LeftOnly && item.status != ItemStatus::RightOnly &&
                                       item.status != ItemStatus::LeftChanged && item.status != ItemStatus::RightChanged))
                continue;
            if (item.status == ItemStatus::Identical && !args.listAll)
                continue;
            fwprintf(stdout, L"%-20ls %ls%ls%ls%ls\n", StatusName(item.status), item.relPath.c_str(), item.isDir ? L"\\" : L"",
                item.note.empty() ? L"" : L"  -- ", item.note.c_str());
        }
        const FolderComparison::Counts counts = comparison.CountFiles();
        if (threeWay)
        {
            fwprintf(stdout, L"%d unchanged, %d changed on left, %d changed on right, %d same on both, %d conflict(s), %d error(s)\n",
                counts.identical, counts.leftChanged, counts.rightChanged, counts.bothSame, counts.conflicts, counts.errors);
            return counts.conflicts || counts.errors ? 1 : 0;
        }
        fwprintf(stdout, L"%d identical, %d different, %d left only, %d right only, %d error(s)\n", counts.identical, counts.different,
            counts.leftOnly, counts.rightOnly, counts.errors);
        return comparison.Item(0).status == ItemStatus::Identical ? 0 : 1;
    }

    int RunBinary(const Arguments& args)
    {
        if (args.files.size() != 2)
            return Usage();
        std::string a, b;
        std::wstring error;
        if (!ReadFileBytes(args.files[0], a, error) || !ReadFileBytes(args.files[1], b, error))
        {
            fwprintf(stderr, L"%ls\n", error.c_str());
            return 2;
        }
        const BinaryDiff diff = DiffBytes(a, b);
        for (const Chunk& chunk : diff.chunks)
        {
            if (!chunk.IsDifference())
                continue;
            fwprintf(stdout, L"0x%08X +%-8d  0x%08X +%d\n", chunk.start[0], chunk.count[0], chunk.start[1], chunk.count[1]);
        }
        fwprintf(stdout, L"%d difference(s), %llu byte(s), sizes %zu and %zu (%ls)\n", diff.Differences(), static_cast<unsigned long long>(diff.differentBytes),
            a.size(), b.size(), diff.aligned ? L"compared offset by offset" : L"insertions and removals found");
        return diff.Differences() ? 1 : 0;
    }

    int RunImage(const Arguments& args)
    {
        if (args.files.size() != 2)
            return Usage();
        Image a, b;
        std::wstring error;
        if (!LoadImageFile(args.files[0], a, error) || !LoadImageFile(args.files[1], b, error))
        {
            fwprintf(stderr, L"%ls\n", error.c_str());
            return 2;
        }
        // --tolerance (parsed with the text options) is the per-channel colour tolerance here.
        const ImageDiff diff = DiffImages(a, b, static_cast<int>(args.options.absoluteTolerance));
        fwprintf(stdout, L"%dx%d and %dx%d: %llu pixel(s) differ in %d area(s), largest channel difference %d\n", a.width, a.height, b.width, b.height,
            static_cast<unsigned long long>(diff.differing), static_cast<int>(diff.regions.size()), diff.maxDelta);
        if (!args.diffImage.empty() && !SaveImagePng(args.diffImage, MakeDifferenceImage(a, diff), error))
        {
            fwprintf(stderr, L"%ls\n", error.c_str());
            return 2;
        }
        return diff.differing ? 1 : 0;
    }

    int RunPatch(const Arguments& args)
    {
        if (args.files.size() != 2)
            return Usage();
        std::string bytes;
        std::wstring error;
        if (!ReadFileBytes(args.files[0], bytes, error))
        {
            fwprintf(stderr, L"%ls\n", error.c_str());
            return 2;
        }
        std::vector<FilePatch> patches = ParsePatch(DecodeText(bytes).text, error);
        if (!error.empty())
            fwprintf(stderr, L"%ls\n", error.c_str());
        int failed = 0;
        for (FilePatch patch : patches)
        {
            if (args.reverse)
                patch = ReversePatch(patch);
            const std::wstring target = args.files[1] + L"\\" + PatchTarget(patch, args.strip);
            if (patch.isDeleted)
            {
                fwprintf(stdout, L"SKIPPED  %ls (deletes the file; delete it yourself)\n", target.c_str());
                continue;
            }
            std::string original;
            TextEncoding encoding;
            if (!patch.isNew)
            {
                if (!ReadFileBytes(target, original, error))
                {
                    fwprintf(stdout, L"FAILED   %ls: %ls\n", target.c_str(), error.c_str());
                    ++failed;
                    continue;
                }
                encoding = DecodeText(original).encoding;
            }
            std::vector<HunkResult> results;
            const std::wstring patched = ApplyPatch(patch.isNew ? std::wstring() : DecodeText(original).text, patch, {}, results);
            int applied = 0;
            for (size_t h = 0; h < results.size(); ++h)
            {
                if (results[h].applied)
                {
                    ++applied;
                    if (results[h].offset || results[h].fuzzy)
                        fwprintf(stdout, L"         hunk %d applied at line %d (offset %+d%ls)\n", static_cast<int>(h) + 1, results[h].line, results[h].offset,
                            results[h].fuzzy ? L", ignoring trailing whitespace" : L"");
                }
                else
                {
                    fwprintf(stdout, L"         hunk %d FAILED\n", static_cast<int>(h) + 1);
                    ++failed;
                }
            }
            fwprintf(stdout, L"%-8ls %ls (%d of %d hunks)\n", applied == static_cast<int>(results.size()) ? L"PATCHED" : L"PARTIAL", target.c_str(), applied,
                static_cast<int>(results.size()));
            if (!args.dryRun && applied > 0 && !WriteFileBytes(target, EncodeText(patched, encoding), error))
            {
                fwprintf(stderr, L"%ls: %ls\n", target.c_str(), error.c_str());
                return 2;
            }
        }
        return failed ? 1 : 0;
    }

    int RunXml(const Arguments& args)
    {
        if (args.files.size() != 1)
            return Usage();
        std::string bytes;
        std::wstring error, formatted;
        if (!ReadFileBytes(args.files[0], bytes, error))
        {
            fwprintf(stderr, L"%ls\n", error.c_str());
            return 2;
        }
        const DecodedText decoded = DecodeText(bytes);
        if (!FormatXml(decoded.text, 2, DominantEol(SplitLines(decoded.text)), formatted, error))
        {
            fwprintf(stderr, L"%ls\n", error.c_str());
            return 2;
        }
        if (args.output.empty())
        {
            fwprintf(stdout, L"%ls", formatted.c_str());
            return 0;
        }
        if (!WriteFileBytes(args.output, EncodeText(formatted, decoded.encoding), error))
        {
            fwprintf(stderr, L"%ls\n", error.c_str());
            return 2;
        }
        return 0;
    }

    int RunDocumentText(const Arguments& args)
    {
        if (args.files.size() != 1)
            return Usage();
        std::wstring text, error;
        if (!ReadDocumentText(args.files[0], text, error))
        {
            fwprintf(stderr, L"%ls: %ls\n", args.files[0].c_str(), error.c_str());
            return 2;
        }
        if (args.output.empty())
        {
            fwprintf(stdout, L"%ls", text.c_str());
            return 0;
        }
        if (!WriteFileBytes(args.output, EncodeText(text, TextEncoding()), error))
        {
            fwprintf(stderr, L"%ls\n", error.c_str());
            return 2;
        }
        return 0;
    }

    // Writes a report to the output file (UTF-8), or to stdout.
    int WriteReport(const Arguments& args, const std::wstring& text)
    {
        if (args.output.empty())
        {
            fwprintf(stdout, L"%ls", text.c_str());
            return 0;
        }
        std::wstring error;
        if (!WriteFileBytes(args.output, EncodeText(text, { EncodingType::Utf8, false, 0 }), error))
        {
            fwprintf(stderr, L"%ls: %ls\n", args.output.c_str(), error.c_str());
            return 2;
        }
        return 0;
    }

    int RunReport(const Arguments& args)
    {
        if (args.files.size() != 2 && args.files.size() != 3)
            return Usage();
        Comparison comparison(static_cast<int>(args.files.size()));
        comparison.SetOptions(args.options);
        for (size_t i = 0; i < args.files.size(); ++i)
        {
            std::wstring error;
            if (!comparison.LoadFile(static_cast<int>(i), args.files[i], error))
            {
                fwprintf(stderr, L"%ls: %ls\n", args.files[i].c_str(), error.c_str());
                return 2;
            }
        }
        ReportOptions options;
        options.format = args.reportFormat;
        options.context = args.context;
        return WriteReport(args, MakeReport(comparison, options));
    }

    int RunFolderReport(const Arguments& args)
    {
        if (args.files.size() != 2 && args.files.size() != 3)
            return Usage();
        FolderOptions folders = args.folders;
        folders.text = args.options;
        FolderComparison comparison(args.files, folders);
        comparison.Run();
        FolderReportOptions options;
        options.xml = args.xmlReport;
        options.includeFileReports = args.fileReports;
        options.onlyDifferences = !args.listAll;
        options.context = args.context < 0 ? 3 : args.context;
        return WriteReport(args, MakeFolderReport(comparison, options));
    }

    int RunInfo(const Arguments& args)
    {
        if (args.files.empty())
            return Usage();
        for (const std::wstring& path : args.files)
        {
            Lines lines;
            TextEncoding encoding;
            if (!Load(path, lines, encoding))
                return 2;
            const Eol eol = DominantEol(lines, Eol::None);
            fwprintf(stdout, L"%ls: %ls, %ls%ls, %d line(s)\n", path.c_str(), EncodingName(encoding).c_str(), EolName(eol),
                HasMixedEol(lines) ? L" (mixed)" : L"", static_cast<int>(lines.size()));
        }
        return 0;
    }

    int RunRoundTrip(const Arguments& args)
    {
        if (args.files.empty())
            return Usage();
        int failures = 0;
        for (const std::wstring& path : args.files)
        {
            std::string bytes;
            std::wstring error;
            if (!ReadFileBytes(path, bytes, error))
            {
                fwprintf(stderr, L"%ls: %ls\n", path.c_str(), error.c_str());
                return 2;
            }
            const DecodedText decoded = DecodeText(bytes);
            const std::string again = EncodeText(JoinLines(SplitLines(decoded.text)), decoded.encoding);
            const bool same = again == bytes;
            failures += !same;
            fwprintf(stdout, L"%ls  %ls  (%ls)\n", same ? L"same     " : L"DIFFERENT", path.c_str(), EncodingName(decoded.encoding).c_str());
        }
        return failures ? 1 : 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);
    if (argc < 2)
        return Usage();
    const std::wstring command = argv[1];
    if (command == L"--version")
    {
        fwprintf(stdout, L"MergeFilesCli %hs\n", MERGEFILES_VERSION_FULL);
        return 0;
    }

    Arguments args;
    for (int i = 2; i < argc; ++i)
    {
        const std::wstring arg = argv[i];
        if (arg == L"-o" && i + 1 < argc)
            args.output = argv[++i];
        else if (arg == L"--markers")
            args.markers = true;
        else if (arg == L"--ignore-whitespace")
            args.options.whitespace = WhitespaceMode::IgnoreChanges;
        else if (arg == L"--ignore-all-whitespace")
            args.options.whitespace = WhitespaceMode::IgnoreAll;
        else if (arg == L"--ignore-case")
            args.options.ignoreCase = true;
        else if (arg == L"--strict-eol")
            args.options.ignoreEol = false;
        else if (arg == L"--ignore-blank-lines")
            args.options.ignoreBlankLines = true;
        else if (arg == L"--dry-run")
            args.dryRun = true;
        else if (arg == L"--all")
            args.listAll = true;
        else if (arg == L"--no-recycle-bin")
            args.folders.useRecycleBin = false;
        else if (arg == L"--xml")
            args.xmlReport = true;
        else if (arg == L"--files")
            args.fileReports = true;
        else if (arg == L"--context" && i + 1 < argc)
            args.context = _wtoi(argv[++i]);
        else if (arg == L"--format" && i + 1 < argc)
        {
            const std::wstring format = argv[++i];
            if (format == L"html")
                args.reportFormat = ReportFormat::Html;
            else if (format == L"slideshow")
                args.reportFormat = ReportFormat::HtmlSlideshow;
            else if (format == L"xml")
                args.reportFormat = ReportFormat::Xml;
            else if (format == L"diff")
                args.reportFormat = ReportFormat::UnifiedDiff;
            else
                return Usage();
        }
        else if (arg == L"--reverse")
            args.reverse = true;
        else if (arg == L"-p" && i + 1 < argc)
            args.strip = _wtoi(argv[++i]);
        else if (arg == L"--diff-image" && i + 1 < argc)
            args.diffImage = argv[++i];
        else if ((arg == L"--mode" || arg == L"--include" || arg == L"--exclude") && i + 1 < argc)
        {
            const std::wstring value = argv[++i];
            if (arg == L"--include")
                args.folders.include = value;
            else if (arg == L"--exclude")
                args.folders.exclude = value;
            else if (value == L"quick")
                args.folders.mode = FolderCompareMode::Quick;
            else if (value == L"binary")
                args.folders.mode = FolderCompareMode::Binary;
            else if (value == L"thorough")
                args.folders.mode = FolderCompareMode::Thorough;
            else
                return Usage();
        }
        else if ((arg == L"--line-expr" || arg == L"--block" || arg == L"--tolerance" || arg == L"--rel-tolerance") && i + 1 < argc)
        {
            const std::wstring value = argv[++i];
            if (arg == L"--tolerance" || arg == L"--rel-tolerance")
            {
                (arg == L"--tolerance" ? args.options.absoluteTolerance : args.options.relativeTolerance) = _wtof(value.c_str());
                continue;
            }
            const std::wstring error = CheckRegex(value);
            if (!error.empty())
            {
                fwprintf(stderr, L"Invalid regular expression %ls: %ls\n", value.c_str(), error.c_str());
                return 2;
            }
            if (arg == L"--line-expr")
            {
                args.options.lineExpressions.push_back(value);
            }
            else
            {
                if (i + 1 >= argc || !CheckRegex(argv[i + 1]).empty())
                    return Usage();
                args.options.blockExpressions.emplace_back(value, argv[++i]);
            }
        }
        else if (arg.size() > 1 && arg[0] == L'-' && arg[1] == L'-')
            return Usage();
        else
            args.files.push_back(arg);
    }

    if (command == L"diff")
        return RunDiff(args);
    if (command == L"diff3")
        return RunDiff3(args);
    if (command == L"merge")
        return RunMerge(args);
    if (command == L"info")
        return RunInfo(args);
    if (command == L"report")
        return RunReport(args);
    if (command == L"dirreport")
        return RunFolderReport(args);
    if (command == L"bincmp")
        return RunBinary(args);
    if (command == L"imgcmp")
        return RunImage(args);
    if (command == L"patch")
        return RunPatch(args);
    if (command == L"xmlfmt")
        return RunXml(args);
    if (command == L"doctext")
        return RunDocumentText(args);
    if (command == L"dircmp")
        return RunFolders(args, false, false);
    if (command == L"dircmp3")
        return RunFolders(args, true, false);
    if (command == L"dirmerge")
        return RunFolders(args, true, true);
    if (command == L"roundtrip")
        return RunRoundTrip(args);
    return Usage();
}
