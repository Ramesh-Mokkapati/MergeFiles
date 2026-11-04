// MergeFilesApp.h - application object: settings (in the registry) and start-up.
#pragma once

#include "framework.h"

#include "CompareWnd.h"
#include "FolderCompare.h"
#include "Output.h"
#include "Printing.h"

namespace mf
{
    class MergeFilesApp : public CWinApp
    {
    public:
        BOOL InitInstance() override;
        int ExitInstance() override;
        // F1 in a dialog box (and Help commands): opens the help at the dialog's or the active tab's topic.
        void WinHelpInternal(DWORD_PTR data, UINT command = HELP_CONTEXT) override;

        void LoadSettings();
        void SaveSettings();

        CompareOptions compareOptions;
        ViewSettings viewSettings;
        FolderOptions folderOptions;
        int imageTolerance = 0;  // per colour channel
        PageSettings pageSettings;
        ReportSettings reportSettings;

    protected:
        afx_msg void OnAppAbout();
        DECLARE_MESSAGE_MAP()
    };

    MergeFilesApp& App();

    // This instance's folder for files extracted from archives (%TEMP%\MergeFiles\<process id>),
    // removed when the program ends.
    std::wstring TempFolder();

    // Opens MergeFiles.html (next to the program) in the browser at an anchor.
    void ShowHelp(const std::wstring& topic);

    // .patch and .diff files open as patches.
    bool IsPatchFileName(const std::wstring& path);

    // Lists of strings stored one per line (registry values, multi-line edit boxes).
    std::vector<std::wstring> SplitList(const CString& text);
    std::wstring JoinList(const std::vector<std::wstring>& items);
}
