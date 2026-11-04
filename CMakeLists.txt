# MergeFiles - 2-way and 3-way file comparison and merging (MFC GUI + CLI).
#
#   powershell -File scripts\build.ps1               # or:
#   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
#   cmake --build build --config Release
#
# Opening build\MergeFiles.sln (or this folder) in Visual Studio 2022 works as well.
cmake_minimum_required(VERSION 3.21)
set(MERGEFILES_VERSION "1.0.0" CACHE STRING "MergeFiles version, major.minor.patch")
set(MERGEFILES_BUILD_NUMBER "0" CACHE STRING "Fourth part of the file version")
project(MergeFiles VERSION ${MERGEFILES_VERSION} LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "MergeFiles is a Windows MFC application: build it with Visual Studio 2022 (MSVC).")
endif()

configure_file(src/Version.h.in "${CMAKE_BINARY_DIR}/generated/Version.h" @ONLY)

# (No WIN32_LEAN_AND_MEAN here: MFC needs the full Windows headers; the engine sources set it themselves.)
set(MERGEFILES_DEFINITIONS UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
set(MERGEFILES_OPTIONS /W4 /utf-8 /EHsc /permissive- /MP)
option(MERGEFILES_WARNINGS_AS_ERRORS "Treat compiler warnings as errors (/WX); the CI build turns this on" OFF)
if(MERGEFILES_WARNINGS_AS_ERRORS)
    list(APPEND MERGEFILES_OPTIONS /WX)
endif()

# ---- engine: diff, 3-way merge, encodings, text buffer with undo (no MFC) ----
add_library(mergefiles_engine STATIC
    src/engine/Encoding.h src/engine/Encoding.cpp
    src/engine/TextLines.h src/engine/TextLines.cpp
    src/engine/Diff.h src/engine/Diff.cpp
    src/engine/Diff3.h src/engine/Diff3.cpp
    src/engine/Compare.cpp
    src/engine/InlineDiff.h src/engine/InlineDiff.cpp
    src/engine/TextBuffer.h src/engine/TextBuffer.cpp
    src/engine/Comparison.h src/engine/Comparison.cpp
    src/engine/Syntax.h src/engine/Syntax.cpp
    src/engine/Search.h src/engine/Search.cpp
    src/engine/FolderCompare.h src/engine/FolderCompare.cpp
    src/engine/BinaryDiff.h src/engine/BinaryDiff.cpp
    src/engine/ImageDiff.h src/engine/ImageDiff.cpp
    src/engine/Patch.h src/engine/Patch.cpp
    src/engine/XmlFormat.h src/engine/XmlFormat.cpp
    src/engine/Xml.h src/engine/Xml.cpp
    src/engine/Report.h src/engine/Report.cpp
    src/engine/Archive.h src/engine/Archive.cpp
    src/engine/Packed.h src/engine/Packed.cpp
    src/engine/DocumentText.h src/engine/DocumentText.cpp)
target_include_directories(mergefiles_engine PUBLIC src/engine)
target_link_libraries(mergefiles_engine PUBLIC shell32 ole32 windowscodecs)

# ---- MergeFilesCli.exe (console): runs the engine on files, for scripts and verification ----
add_executable(MergeFilesCli src/cli/main_cli.cpp)
target_link_libraries(MergeFilesCli PRIVATE mergefiles_engine)

# ---- MergeFilesTests.exe: engine unit tests (ctest, or run the exe) ----
add_executable(MergeFilesTests tests/TestMain.cpp tests/TestFramework.h
    tests/EncodingTests.cpp tests/DiffTests.cpp tests/Diff3Tests.cpp tests/BufferTests.cpp tests/MiscTests.cpp tests/RulesTests.cpp tests/FolderTests.cpp tests/MediaTests.cpp tests/ReportTests.cpp tests/DocumentTests.cpp)
target_link_libraries(MergeFilesTests PRIVATE mergefiles_engine)
target_compile_definitions(MergeFilesTests PRIVATE MERGEFILES_TESTDATA=L"${CMAKE_SOURCE_DIR}/testdata")
enable_testing()
add_test(NAME engine COMMAND MergeFilesTests)

# ---- MergeFiles.exe (MFC frame window, MFC in a shared DLL) ----
set(CMAKE_MFC_FLAG 2)
add_executable(MergeFiles WIN32
    src/app/framework.h src/app/resource.h
    src/app/MergeFilesApp.h src/app/MergeFilesApp.cpp
    src/app/MainFrame.h src/app/MainFrame.cpp
    src/app/Theme.h src/app/Theme.cpp
    src/app/CommandBar.h src/app/CommandBar.cpp
    src/app/TabStrip.h src/app/TabStrip.cpp
    src/app/CompareWnd.h src/app/CompareWnd.cpp
    src/app/PaneView.h src/app/PaneView.cpp
    src/app/LinkGutter.h src/app/LinkGutter.cpp
    src/app/OverviewStrip.h src/app/OverviewStrip.cpp
    src/app/Dialogs.h src/app/Dialogs.cpp
    src/app/OptionsDlg.h src/app/OptionsDlg.cpp
    src/app/Printing.h src/app/Printing.cpp
    src/app/Output.h src/app/Output.cpp
    src/app/LineDetailPanel.h src/app/LineDetailPanel.cpp
    src/app/TabWindow.h
    src/app/FolderTreeView.h src/app/FolderTreeView.cpp
    src/app/FolderCompareWnd.h src/app/FolderCompareWnd.cpp
    src/app/BinaryCompareWnd.h src/app/BinaryCompareWnd.cpp
    src/app/ImageCompareWnd.h src/app/ImageCompareWnd.cpp
    src/app/PatchWnd.h src/app/PatchWnd.cpp
    src/app/MergeFiles.rc src/app/MergeFiles.manifest)
target_compile_definitions(MergeFiles PRIVATE _AFXDLL)
target_link_libraries(MergeFiles PRIVATE mergefiles_engine imm32 shlwapi)
# The help (F1) is MergeFiles.html next to the program.
add_custom_command(TARGET MergeFiles POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/docs/help.html" "$<TARGET_FILE_DIR:MergeFiles>/MergeFiles.html")
target_link_options(MergeFiles PRIVATE /ENTRY:wWinMainCRTStartup)

foreach(target mergefiles_engine MergeFilesCli MergeFilesTests MergeFiles)
    set_target_properties(${target} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    target_compile_definitions(${target} PRIVATE ${MERGEFILES_DEFINITIONS})
    target_compile_options(${target} PRIVATE ${MERGEFILES_OPTIONS})
endforeach()
foreach(target MergeFilesCli MergeFilesTests MergeFiles)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    target_include_directories(${target} PRIVATE "${CMAKE_BINARY_DIR}/generated")   # Version.h (also for the .rc)
endforeach()
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT MergeFiles)
