// MergeFilesApp.cpp - start-up, command line and settings.
//
//   MergeFiles.exe                         empty window (File > New, or drop files on it)
//   MergeFiles.exe <left> <right>          two-way comparison
//   MergeFiles.exe <left> <ancestor> <right>  three-way comparison, merging into the ancestor
#include "MergeFilesApp.h"

#include <filesystem>
#include <iterator>

#include <afxpriv.h>  // HID_BASE_RESOURCE
#include <shlwapi.h>

#include "Dialogs.h"
#include "Theme.h"
#include "MainFrame.h"
#include "Version.h"
#include "resource.h"

namespace mf
{
    namespace
    {
        MergeFilesApp theApp;
    }

    MergeFilesApp& App()
    {
        return theApp;
    }

    bool IsPatchFileName(const std::wstring& path)
    {
        const size_t dot = path.find_last_of(L'.');
        if (dot == std::wstring::npos)
            return false;
        const std::wstring ext = path.substr(dot);
        return _wcsicmp(ext.c_str(), L".patch") == 0 || _wcsicmp(ext.c_str(), L".diff") == 0;
    }

    std::vector<std::wstring> SplitList(const CString& text)
    {
        std::vector<std::wstring> items;
        std::wstring all(text);
        size_t start = 0;
        while (start <= all.size())
        {
            size_t end = all.find(L'\n', start);
            if (end == std::wstring::npos)
                end = all.size();
            std::wstring item = all.substr(start, end - start);
            if (!item.empty() && item.back() == L'\r')
                item.pop_back();
            if (!item.empty())
                items.push_back(item);
            start = end + 1;
        }
        return items;
    }

    std::wstring JoinList(const std::vector<std::wstring>& items)
    {
        std::wstring text;
        for (const std::wstring& item : items)
        {
            if (!text.empty())
                text += L"\n";
            text += item;
        }
        return text;
    }

    BEGIN_MESSAGE_MAP(MergeFilesApp, CWinApp)
        ON_COMMAND(ID_APP_ABOUT, &MergeFilesApp::OnAppAbout)
        ON_COMMAND(ID_HELP, &CWinApp::OnHelp)
    END_MESSAGE_MAP()

    BOOL MergeFilesApp::InitInstance()
    {
        INITCOMMONCONTROLSEX controls = { sizeof(controls), ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&controls);
        CWinApp::InitInstance();
        SetRegistryKey(L"MergeFiles");
        LoadSettings();

        auto* frame = new MainFrame;
        m_pMainWnd = frame;
        if (!frame->LoadFrame(IDR_MAINFRAME, WS_OVERLAPPEDWINDOW))
            return FALSE;

        // Restore the last window position.
        WINDOWPLACEMENT* placement = nullptr;
        UINT size = 0;
        if (GetProfileBinary(L"Window", L"Placement", reinterpret_cast<LPBYTE*>(&placement), &size) && size == sizeof(WINDOWPLACEMENT))
        {
            placement->length = sizeof(WINDOWPLACEMENT);
            if (placement->showCmd == SW_SHOWMINIMIZED)
                placement->showCmd = SW_SHOWNORMAL;
            frame->SetWindowPlacement(placement);
        }
        else
        {
            frame->ShowWindow(SW_SHOWMAXIMIZED);
        }
        delete[] reinterpret_cast<BYTE*>(placement);
        frame->ShowWindow(SW_SHOW);
        frame->UpdateWindow();

        std::vector<std::wstring> files;
        for (int i = 1; i < __argc; ++i)
        {
            const std::wstring arg = __wargv[i];
            if (!arg.empty() && arg[0] != L'/' && arg[0] != L'-')
                files.push_back(arg);
        }
        if (files.size() == 1 && IsPatchFileName(files[0]))
            frame->OpenPatch(files[0]);
        else if (files.size() == 1 && IsArchiveFileName(files[0]))
            frame->OpenArchive(files[0]);
        else if (files.size() == 2 || files.size() == 3)
            frame->OpenComparison(files);
        else if (!files.empty())
            AfxMessageBox(L"Usage: MergeFiles.exe <left> <right>  or  MergeFiles.exe <left> <ancestor> <right>\n(two or three files, or two or three folders)", MB_ICONINFORMATION);
        return TRUE;
    }

    std::wstring TempFolder()
    {
        wchar_t temp[MAX_PATH + 1] = {};
        GetTempPathW(MAX_PATH, temp);
        return std::wstring(temp) + L"MergeFiles\\" + std::to_wstring(GetCurrentProcessId());
    }

    void ShowHelp(const std::wstring& topic)
    {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring path(exe);
        path = path.substr(0, path.find_last_of(L'\\') + 1) + L"MergeFiles.html";
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            AfxMessageBox((L"The help file is missing:\n" + path).c_str(), MB_ICONEXCLAMATION);
            return;
        }
        // Opening the file itself would drop the "#topic" part, so the browser is given the URL.
        std::wstring url = L"file:///";
        for (wchar_t c : path)
        {
            if (c == L'\\')
                url += L'/';
            else if (c == L' ')
                url += L"%20";
            else if (c == L'#')
                url += L"%23";
            else if (c == L'%')
                url += L"%25";
            else
                url += c;
        }
        url += L"#" + topic;
        wchar_t browser[MAX_PATH * 2] = {};
        DWORD size = static_cast<DWORD>(std::size(browser));
        const HWND owner = AfxGetMainWnd() ? AfxGetMainWnd()->GetSafeHwnd() : nullptr;
        if (SUCCEEDED(AssocQueryStringW(ASSOCF_IS_PROTOCOL, ASSOCSTR_EXECUTABLE, L"https", L"open", browser, &size)) &&
            reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", browser, (L"\"" + url + L"\"").c_str(), nullptr, SW_SHOWNORMAL)) > 32)
            return;
        ShellExecuteW(owner, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void MergeFilesApp::WinHelpInternal(DWORD_PTR data, UINT)
    {
        const wchar_t* topic = nullptr;
        if (data >= HID_BASE_RESOURCE && data < HID_BASE_RESOURCE + 0x10000)
        {
            switch (static_cast<UINT>(data - HID_BASE_RESOURCE))
            {
            case IDD_NEW_COMPARISON:
                topic = L"new-comparison";
                break;
            case IDD_FIND_REPLACE:
            case IDD_GOTO_LINE:
                topic = L"find";
                break;
            case IDD_STATISTICS:
                topic = L"statistics";
                break;
            case IDD_OPT_COMPARE:
                topic = L"opt-compare";
                break;
            case IDD_OPT_RULES:
                topic = L"opt-rules";
                break;
            case IDD_OPT_DISPLAY:
                topic = L"opt-display";
                break;
            case IDD_OPT_FOLDERS:
                topic = L"opt-folders";
                break;
            case IDD_FOLDER_SELECT:
                topic = L"select-files";
                break;
            case IDD_REPORT:
                topic = L"reports";
                break;
            case IDD_COMMENT:
                topic = L"bookmarks";
                break;
            case IDD_PAGE_SETUP:
            case IDD_PRINT_PREVIEW:
                topic = L"printing";
                break;
            default:
                break;
            }
        }
        if (!topic)
        {
            MainFrame* frame = dynamic_cast<MainFrame*>(m_pMainWnd);
            TabWindow* tab = frame ? frame->Active() : nullptr;
            topic = tab ? tab->HelpTopic() : L"overview";
        }
        ShowHelp(topic);
    }

    int MergeFilesApp::ExitInstance()
    {
        std::error_code ignored;
        std::filesystem::remove_all(TempFolder(), ignored);
        SaveSettings();
        return CWinApp::ExitInstance();
    }

    void MergeFilesApp::LoadSettings()
    {
        const int whitespace = GetProfileInt(L"Options", L"Whitespace", 0);
        compareOptions.whitespace = whitespace == 1 ? WhitespaceMode::IgnoreChanges : (whitespace == 2 ? WhitespaceMode::IgnoreAll : WhitespaceMode::Compare);
        compareOptions.ignoreCase = GetProfileInt(L"Options", L"IgnoreCase", 0) != 0;
        compareOptions.ignoreEol = GetProfileInt(L"Options", L"IgnoreLineEndings", 1) != 0;
        viewSettings.syntax = GetProfileInt(L"Options", L"Syntax", 1) != 0;
        viewSettings.showWhitespace = GetProfileInt(L"Options", L"ShowWhitespace", 0) != 0;
        viewSettings.tabSize = (std::max)(1, (std::min)(16, static_cast<int>(GetProfileInt(L"Options", L"TabSize", 4))));
        viewSettings.wordWrap = GetProfileInt(L"Options", L"WordWrap", 0) != 0;
        viewSettings.verticalPadding = GetProfileInt(L"Options", L"VerticalPadding", 0) != 0;
        viewSettings.lineDetail = GetProfileInt(L"Options", L"LineDetail", 0) != 0;
        compareOptions.ignoreBlankLines = GetProfileInt(L"Options", L"IgnoreBlankLines", 0) != 0;
        compareOptions.absoluteTolerance = _wtof(GetProfileString(L"Options", L"AbsoluteTolerance", L"0"));
        compareOptions.relativeTolerance = _wtof(GetProfileString(L"Options", L"RelativeTolerance", L"0"));
        compareOptions.linePairingRule = std::wstring(GetProfileString(L"Options", L"LinePairingRule", L""));
        compareOptions.lineExpressions = SplitList(GetProfileString(L"Options", L"LineExpressions", L""));
        compareOptions.blockExpressions.clear();
        for (const std::wstring& entry : SplitList(GetProfileString(L"Options", L"BlockExpressions", L"")))
        {
            const size_t tab = entry.find(L'\t');
            if (tab != std::wstring::npos)
                compareOptions.blockExpressions.emplace_back(entry.substr(0, tab), entry.substr(tab + 1));
        }
        const int folderMode = GetProfileInt(L"Folders", L"Mode", 1);
        folderOptions.mode = folderMode == 0 ? FolderCompareMode::Quick : (folderMode == 2 ? FolderCompareMode::Thorough : FolderCompareMode::Binary);
        folderOptions.thoroughLimit = static_cast<uint64_t>((std::max)(1, static_cast<int>(GetProfileInt(L"Folders", L"ThoroughLimitMB", 16)))) * 1024 * 1024;
        folderOptions.include = std::wstring(GetProfileString(L"Folders", L"Include", L""));
        folderOptions.exclude = std::wstring(GetProfileString(L"Folders", L"Exclude", L".git\\;.svn\\;.hg\\;.vs\\"));
        imageTolerance = (std::max)(0, (std::min)(255, static_cast<int>(GetProfileInt(L"Images", L"Tolerance", 0))));
        GetFonts().SetCodeFont(std::wstring(GetProfileString(L"Display", L"FontFace", L"")), static_cast<int>(GetProfileInt(L"Display", L"FontSize", 10)));
        pageSettings.header = std::wstring(GetProfileString(L"Printing", L"Header", pageSettings.header.c_str()));
        pageSettings.footer = std::wstring(GetProfileString(L"Printing", L"Footer", pageSettings.footer.c_str()));
        pageSettings.landscape = GetProfileInt(L"Printing", L"Landscape", 1) != 0;
        pageSettings.changedPagesOnly = GetProfileInt(L"Printing", L"ChangedPagesOnly", 0) != 0;
        pageSettings.fontPoints = (std::max)(5, (std::min)(24, static_cast<int>(GetProfileInt(L"Printing", L"FontSize", 8))));
        reportSettings.format = (std::max)(0, (std::min)(3, static_cast<int>(GetProfileInt(L"Reports", L"Format", 0))));
        reportSettings.wholeFiles = GetProfileInt(L"Reports", L"WholeFiles", 0) != 0;
        reportSettings.context = (std::max)(0, (std::min)(1000, static_cast<int>(GetProfileInt(L"Reports", L"Context", 3))));
        reportSettings.fileReports = GetProfileInt(L"Reports", L"FileReports", 1) != 0;
        reportSettings.identical = GetProfileInt(L"Reports", L"Identical", 0) != 0;
        reportSettings.openWhenSaved = GetProfileInt(L"Reports", L"Open", 0) != 0;
        Theme& theme = EditableTheme();
        for (const ThemeColor& color : ThemeColors())
            theme.*color.member = static_cast<COLORREF>(GetProfileInt(L"Colours", color.key, static_cast<int>(theme.*color.member)));
    }

    void MergeFilesApp::SaveSettings()
    {
        WriteProfileInt(L"Options", L"Whitespace", compareOptions.whitespace == WhitespaceMode::IgnoreChanges ? 1 : (compareOptions.whitespace == WhitespaceMode::IgnoreAll ? 2 : 0));
        WriteProfileInt(L"Options", L"IgnoreCase", compareOptions.ignoreCase);
        WriteProfileInt(L"Options", L"IgnoreLineEndings", compareOptions.ignoreEol);
        WriteProfileInt(L"Options", L"Syntax", viewSettings.syntax);
        WriteProfileInt(L"Options", L"ShowWhitespace", viewSettings.showWhitespace);
        WriteProfileInt(L"Options", L"TabSize", viewSettings.tabSize);
        WriteProfileInt(L"Options", L"WordWrap", viewSettings.wordWrap);
        WriteProfileInt(L"Options", L"VerticalPadding", viewSettings.verticalPadding);
        WriteProfileInt(L"Options", L"LineDetail", viewSettings.lineDetail);
        WriteProfileInt(L"Options", L"IgnoreBlankLines", compareOptions.ignoreBlankLines);
        CString number;
        number.Format(L"%.15g", compareOptions.absoluteTolerance);
        WriteProfileString(L"Options", L"AbsoluteTolerance", number);
        number.Format(L"%.15g", compareOptions.relativeTolerance);
        WriteProfileString(L"Options", L"RelativeTolerance", number);
        WriteProfileString(L"Options", L"LinePairingRule", compareOptions.linePairingRule.c_str());
        WriteProfileString(L"Options", L"LineExpressions", JoinList(compareOptions.lineExpressions).c_str());
        std::vector<std::wstring> blocks;
        for (const auto& block : compareOptions.blockExpressions)
            blocks.push_back(block.first + L"\t" + block.second);
        WriteProfileString(L"Options", L"BlockExpressions", JoinList(blocks).c_str());
        WriteProfileInt(L"Folders", L"Mode", folderOptions.mode == FolderCompareMode::Quick ? 0 : (folderOptions.mode == FolderCompareMode::Thorough ? 2 : 1));
        WriteProfileInt(L"Folders", L"ThoroughLimitMB", static_cast<int>(folderOptions.thoroughLimit / (1024 * 1024)));
        WriteProfileString(L"Folders", L"Include", folderOptions.include.c_str());
        WriteProfileString(L"Folders", L"Exclude", folderOptions.exclude.c_str());
        WriteProfileInt(L"Images", L"Tolerance", imageTolerance);
        WriteProfileString(L"Display", L"FontFace", GetFonts().CodeFace().c_str());
        WriteProfileInt(L"Display", L"FontSize", GetFonts().CodePointSize());
        WriteProfileString(L"Printing", L"Header", pageSettings.header.c_str());
        WriteProfileString(L"Printing", L"Footer", pageSettings.footer.c_str());
        WriteProfileInt(L"Printing", L"Landscape", pageSettings.landscape);
        WriteProfileInt(L"Printing", L"ChangedPagesOnly", pageSettings.changedPagesOnly);
        WriteProfileInt(L"Printing", L"FontSize", pageSettings.fontPoints);
        WriteProfileInt(L"Reports", L"Format", reportSettings.format);
        WriteProfileInt(L"Reports", L"WholeFiles", reportSettings.wholeFiles);
        WriteProfileInt(L"Reports", L"Context", reportSettings.context);
        WriteProfileInt(L"Reports", L"FileReports", reportSettings.fileReports);
        WriteProfileInt(L"Reports", L"Identical", reportSettings.identical);
        WriteProfileInt(L"Reports", L"Open", reportSettings.openWhenSaved);
        const Theme defaults;
        for (const ThemeColor& color : ThemeColors())
        {
            // Only colours changed from the defaults are stored, so new defaults reach old installs.
            if (GetTheme().*color.member != defaults.*color.member)
                WriteProfileInt(L"Colours", color.key, static_cast<int>(GetTheme().*color.member));
            else
                WriteProfileString(L"Colours", color.key, nullptr);
        }
    }

    void MergeFilesApp::OnAppAbout()
    {
        CString version;
        version.Format(L"Version %hs", MERGEFILES_VERSION_FULL);
        TextDlg dialog(IDD_ABOUT, version, m_pMainWnd);
        dialog.DoModal();
    }
}
