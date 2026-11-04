# MergeFiles: phased plan

Requirement numbers refer to the line numbers in `Requirements.txt`.

## Layout

```
MergeFiles/
  CMakeLists.txt            VS 2022 generator, x64, MFC as a shared DLL, /W4 /WX /utf-8 /permissive-
  .clang-format             Allman, copied from AppAssist's conventions
  scripts/build.ps1         finds VS 2022's CMake through vswhere; builds to build\bin\Release
  scripts/test.ps1          builds, then runs the unit tests and CLI checks on testdata\
  src/engine/               static library, no MFC: diff, 3-way merge, encodings, text model
  src/cli/                  MergeFilesCli.exe: diff, diff3, merge, encode (verifies the engine)
  src/app/                  MergeFiles.exe: the MFC GUI
  tests/                    unit tests for the engine (a small built-in test runner, no downloads)
  testdata/                 sample files: 2-way and 3-way, with each encoding and line ending
  README.md                 build, test and layout sections
```

## Phase 1: working core (done, 2026-11-03)

Engine (`MergeFilesEngine.lib`):
- Myers O(ND) diff with a linear-space middle snake, plus a pass that groups the output into blocks (insert, remove, change)
- comparison options: ignore whitespace (all, leading/trailing, amount), ignore case, ignore line-ending differences
- intra-line character/word diff for each changed line pair
- 3-way diff3: aligns base↔left and base↔right, classifies each block as left-only, right-only, same-both or conflict, then auto-merges into a result with conflict flags
- encodings: detect and round-trip UTF-8 and UTF-16 LE/BE (with and without BOM) and ANSI/MBCS code pages; detect and preserve CR LF, LF and CR line endings
- an undoable editing model (`TextBuffer` + command-based undo/redo) shared by the GUI

GUI (`MergeFiles.exe`, MDI with a tab strip):
- toolbar groups: New Comparison, Start/Stop, Changes, Recompare, Layout (2/3 panes), Comparison, Settings, Current Pane, Find, Undo/Redo, Edits, Indent
- each pane: a path bar (read-only and modified indicators), line numbers, syntax highlighting (C/C++, C#, Java/JS, Python, XML/HTML, INI, plain text), block tints (green insert, purple change, orange remove), character-level highlights
- linking-line gutters between panes with no padding lines, synchronized scrolling centred on related blocks, and a copy-to-other-side arrow button on each block
- overview strips: clicking one jumps to that change
- next/previous change and next/previous conflict, automatic merge (undoable), mark as resolved (dimmed), edit markers
- in-place editing: caret, selection, clipboard, undo/redo, block indent/un-indent, find/replace, go to line
- open dialog, drag and drop from Explorer, command line (`MergeFiles a b [c]`), save, save as, encoding/line-ending selection per pane
- status bar: encoding, removals, insertions and changes, line and column
- tab strip entries for file comparisons; folder and image tabs appear in later phases

Verification: unit tests for the engine, the CLI run on testdata (round-trips are byte-identical and merge output matches the expected files), a /W4 build with zero warnings, and the GUI launched on the sample files.

## Phase 2: text comparison depth (done, 2026-11-04)
Done: line expressions and block expressions (regex ignores, shown as grey ignored differences), ignore blank lines, numeric tolerance (absolute and relative), line-pairing rules, synchronization links, optional vertical padding, word wrap, the line-detail panel, bookmarks, drag-and-drop text editing (move, or copy with Ctrl), customizable fonts and colours, and an Options dialog persisted in the registry. Already in Phase 1: showing line-ending characters, comparison statistics, Replace File commands and EUC-JP.

Not done: bookmark comments (moved to Phase 5, with comparison archives), and checking IME input with a real Japanese/Chinese IME. The composition window is positioned at the caret, but this hasn't been tried by hand.

## Phase 3: folder comparison (done, 2026-11-04)
2-way and 3-way folder comparison (quick, byte-by-byte and thorough modes), visibility and selection filters, synchronization, automatic folder merge, and a file comparison split shown below the folder view.

## Phase 4: other file types (done, 2026-11-04)
Binary (hex) comparison, image comparison (WIC, pixel diff), patch files (view and selectively apply), and XML display formatting.

## Phase 5: reports, archives, printing (done, 2026-11-04)
HTML, HTML slideshow, XML and unified-diff reports (file and folder, with optional file reports); bookmark comments; comparison archives (.mfcmp) for file comparisons (texts, encodings, rules, bookmarks, comments, resolved lines, sync links) and folder comparisons (results, opened read-only); printing and print preview with headers and footers, orientation, font size and a changed-pages-only option.

Not done: printing was checked in the print preview only, not on a physical printer.

## Phase 6: documents and archives (done, 2026-11-04)
Text from Word, Excel, PowerPoint, OpenDocument and RTF files, compared read-only and used by thorough folder comparisons. A built-in DEFLATE/gzip decoder with zip and tar readers, so archives can be compared as folders (read-only; copying out extracts). Context help (F1) in an HTML help file, per tab and per dialog.

Set aside by the user (2026-11-04): PDF text extraction (a built-in extractor or the PDFium library) and an installer (an own setup program, a WiX MSI or an MSIX package). Both are open for a later phase.
