// Dialogs.cpp - the small dialogs.
#include "Dialogs.h"

#include "CompareWnd.h"
#include "Packed.h"
#include "PaneView.h"

namespace mf
{
    // ---- New Comparison ----

    BEGIN_MESSAGE_MAP(NewComparisonDlg, CDialog)
        ON_BN_CLICKED(IDC_NC_TWO_WAY, &NewComparisonDlg::OnModeChanged)
        ON_BN_CLICKED(IDC_NC_THREE_WAY, &NewComparisonDlg::OnModeChanged)
        ON_BN_CLICKED(IDC_NC_FOLDERS, &NewComparisonDlg::OnModeChanged)
        ON_COMMAND_RANGE(IDC_NC_BROWSE_LEFT, IDC_NC_BROWSE_RIGHT, &NewComparisonDlg::OnBrowse)
    END_MESSAGE_MAP()

    NewComparisonDlg::NewComparisonDlg(bool threeWayMode, bool folderMode, CWnd* parent) : CDialog(IDD_NEW_COMPARISON, parent), threeWay(threeWayMode), folders(folderMode)
    {
    }

    BOOL NewComparisonDlg::OnInitDialog()
    {
        CDialog::OnInitDialog();
        CheckRadioButton(IDC_NC_TWO_WAY, IDC_NC_THREE_WAY, threeWay ? IDC_NC_THREE_WAY : IDC_NC_TWO_WAY);
        CheckDlgButton(IDC_NC_FOLDERS, folders ? BST_CHECKED : BST_UNCHECKED);
        // Remember the last files (or folders) chosen.
        CWinApp* app = AfxGetApp();
        const CString suffix = folders ? L"Folder" : L"";
        SetDlgItemText(IDC_NC_LEFT, app->GetProfileString(L"Recent", L"Left" + suffix));
        SetDlgItemText(IDC_NC_MIDDLE, app->GetProfileString(L"Recent", L"Ancestor" + suffix));
        SetDlgItemText(IDC_NC_RIGHT, app->GetProfileString(L"Recent", L"Right" + suffix));
        UpdateMode();
        GetDlgItem(IDC_NC_LEFT)->SetFocus();
        return FALSE;
    }

    void NewComparisonDlg::UpdateMode()
    {
        threeWay = IsDlgButtonChecked(IDC_NC_THREE_WAY) == BST_CHECKED;
        folders = IsDlgButtonChecked(IDC_NC_FOLDERS) == BST_CHECKED;
        SetDlgItemText(IDC_NC_LEFT_LABEL, folders ? L"&Left folder:" : L"&Left file:");
        SetDlgItemText(IDC_NC_RIGHT_LABEL, folders ? L"&Right folder:" : L"&Right file:");
        GetDlgItem(IDC_NC_MIDDLE)->EnableWindow(threeWay);
        GetDlgItem(IDC_NC_BROWSE_MIDDLE)->EnableWindow(threeWay);
        GetDlgItem(IDC_NC_MIDDLE_LABEL)->EnableWindow(threeWay);
    }

    void NewComparisonDlg::OnModeChanged()
    {
        UpdateMode();
    }

    void NewComparisonDlg::OnBrowse(UINT id)
    {
        const UINT edit = id == IDC_NC_BROWSE_LEFT ? IDC_NC_LEFT : (id == IDC_NC_BROWSE_MIDDLE ? IDC_NC_MIDDLE : IDC_NC_RIGHT);
        CString current;
        GetDlgItemText(edit, current);
        UpdateMode();
        if (folders)
        {
            CFolderPickerDialog dialog(current, 0, this);
            if (dialog.DoModal() == IDOK)
                SetDlgItemText(edit, dialog.GetPathName());
            return;
        }
        CFileDialog dialog(TRUE, nullptr, current, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, L"All files (*.*)|*.*||", this);
        if (dialog.DoModal() == IDOK)
            SetDlgItemText(edit, dialog.GetPathName());
    }

    void NewComparisonDlg::OnOK()
    {
        UpdateMode();
        paths.clear();
        const UINT ids[3] = { IDC_NC_LEFT, IDC_NC_MIDDLE, IDC_NC_RIGHT };
        for (UINT id : ids)
        {
            if (id == IDC_NC_MIDDLE && !threeWay)
                continue;
            CString path;
            GetDlgItemText(id, path);
            path.Trim();
            path.Trim(L'"');
            if (path.IsEmpty() && id != IDC_NC_MIDDLE)
            {
                AfxMessageBox(L"Choose a file for each side.", MB_ICONINFORMATION);
                GetDlgItem(id)->SetFocus();
                return;
            }
            const DWORD attributes = path.IsEmpty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(path);
            // A folder comparison also takes archives (.zip, .tar, ...) in place of folders.
            const bool isFolder = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const bool isArchive = attributes != INVALID_FILE_ATTRIBUTES && !isFolder && IsPackedArchiveName(std::wstring(path));
            if (!path.IsEmpty() && (attributes == INVALID_FILE_ATTRIBUTES || (folders ? !(isFolder || isArchive) : isFolder)))
            {
                AfxMessageBox((folders ? L"Folder or archive not found:\n" : L"File not found:\n") + path, MB_ICONEXCLAMATION);
                GetDlgItem(id)->SetFocus();
                return;
            }
            if (path.IsEmpty() && folders)
            {
                AfxMessageBox(L"Choose a folder for each side.", MB_ICONINFORMATION);
                GetDlgItem(id)->SetFocus();
                return;
            }
            paths.push_back(std::wstring(path));
        }
        CWinApp* app = AfxGetApp();
        const CString suffix = folders ? L"Folder" : L"";
        app->WriteProfileString(L"Recent", L"Left" + suffix, paths.front().c_str());
        app->WriteProfileString(L"Recent", L"Right" + suffix, paths.back().c_str());
        if (threeWay)
            app->WriteProfileString(L"Recent", L"Ancestor" + suffix, paths[1].c_str());
        CDialog::OnOK();
    }

    // ---- Select Files ----

    BOOL SelectFilesDlg::OnInitDialog()
    {
        CDialog::OnInitDialog();
        SetDlgItemText(IDC_FS_PATTERN, pattern.c_str());
        CheckDlgButton(IDC_FS_IDENTICAL, identical);
        CheckDlgButton(IDC_FS_LEFT, left);
        CheckDlgButton(IDC_FS_RIGHT, right);
        CheckDlgButton(IDC_FS_DIFFERENT, different);
        return TRUE;
    }

    void SelectFilesDlg::OnOK()
    {
        CString text;
        GetDlgItemText(IDC_FS_PATTERN, text);
        text.Trim();
        pattern = std::wstring(text);
        identical = IsDlgButtonChecked(IDC_FS_IDENTICAL) != 0;
        left = IsDlgButtonChecked(IDC_FS_LEFT) != 0;
        right = IsDlgButtonChecked(IDC_FS_RIGHT) != 0;
        different = IsDlgButtonChecked(IDC_FS_DIFFERENT) != 0;
        CDialog::OnOK();
    }

    // ---- Find and Replace ----

    BEGIN_MESSAGE_MAP(FindReplaceDlg, CDialog)
        ON_BN_CLICKED(IDC_FR_FIND_NEXT, &FindReplaceDlg::OnFindNext)
        ON_BN_CLICKED(IDC_FR_FIND_PREV, &FindReplaceDlg::OnFindPrev)
        ON_BN_CLICKED(IDC_FR_REPLACE_ONE, &FindReplaceDlg::OnReplace)
        ON_BN_CLICKED(IDC_FR_REPLACE_ALL, &FindReplaceDlg::OnReplaceAll)
    END_MESSAGE_MAP()

    FindReplaceDlg::FindReplaceDlg(std::function<CompareWnd*()> target) : m_target(std::move(target))
    {
    }

    BOOL FindReplaceDlg::Create(CWnd* parent)
    {
        return CDialog::Create(IDD_FIND_REPLACE, parent);
    }

    BOOL FindReplaceDlg::OnInitDialog()
    {
        CDialog::OnInitDialog();
        CheckDlgButton(IDC_FR_WRAP, BST_CHECKED);
        return TRUE;
    }

    void FindReplaceDlg::PostNcDestroy()
    {
        // Owned by the frame (a member), not deleted here.
    }

    void FindReplaceDlg::Show(bool replace, const CString& initialText)
    {
        if (!initialText.IsEmpty())
            SetDlgItemText(IDC_FR_FIND, initialText);
        SetWindowText(replace ? L"Replace" : L"Find");
        if (!IsWindowVisible())
        {
            // Open near the top right of the main window.
            CRect frame, rect;
            GetParent()->GetWindowRect(&frame);
            GetWindowRect(&rect);
            SetWindowPos(nullptr, frame.right - rect.Width() - 40, frame.top + 160, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        }
        ShowWindow(SW_SHOW);
        SetActiveWindow();
        CEdit* edit = static_cast<CEdit*>(GetDlgItem(replace && !initialText.IsEmpty() ? IDC_FR_REPLACE : IDC_FR_FIND));
        edit->SetFocus();
        edit->SetSel(0, -1);
        SetStatus(L"");
    }

    std::wstring FindReplaceDlg::What() const
    {
        CString text;
        GetDlgItemText(IDC_FR_FIND, text);
        return std::wstring(text);
    }

    void FindReplaceDlg::SetStatus(const CString& text)
    {
        SetDlgItemText(IDC_FR_STATUS, text);
    }

    bool FindReplaceDlg::Find(bool backward)
    {
        CompareWnd* target = m_target();
        const std::wstring what = What();
        if (!target || what.empty())
            return false;
        m_lastText = what.c_str();
        m_hasLast = true;
        SearchOptions options;
        options.matchCase = IsDlgButtonChecked(IDC_FR_MATCH_CASE) == BST_CHECKED;
        options.wholeWord = IsDlgButtonChecked(IDC_FR_WHOLE_WORD) == BST_CHECKED;
        options.wrap = IsDlgButtonChecked(IDC_FR_WRAP) == BST_CHECKED;
        options.backward = backward;
        const bool found = target->FindNext(what, options, IsDlgButtonChecked(IDC_FR_SELECTION) == BST_CHECKED);
        SetStatus(found ? L"" : L"Not found.");
        return found;
    }

    bool FindReplaceDlg::FindAgain(bool backward)
    {
        if (!m_hWnd || What().empty())
            return false;
        return Find(backward);
    }

    void FindReplaceDlg::OnFindNext()
    {
        Find(false);
    }

    void FindReplaceDlg::OnFindPrev()
    {
        Find(true);
    }

    void FindReplaceDlg::OnOK()
    {
        Find(false);
    }

    void FindReplaceDlg::OnReplace()
    {
        CompareWnd* target = m_target();
        if (!target || What().empty())
            return;
        CString with;
        GetDlgItemText(IDC_FR_REPLACE, with);
        SearchOptions options;
        options.matchCase = IsDlgButtonChecked(IDC_FR_MATCH_CASE) == BST_CHECKED;
        options.wholeWord = IsDlgButtonChecked(IDC_FR_WHOLE_WORD) == BST_CHECKED;
        options.wrap = IsDlgButtonChecked(IDC_FR_WRAP) == BST_CHECKED;
        const bool found = target->ReplaceCurrent(What(), std::wstring(with), options, IsDlgButtonChecked(IDC_FR_SELECTION) == BST_CHECKED);
        SetStatus(found ? L"" : L"Not found.");
    }

    void FindReplaceDlg::OnReplaceAll()
    {
        CompareWnd* target = m_target();
        if (!target || What().empty())
            return;
        CString with;
        GetDlgItemText(IDC_FR_REPLACE, with);
        SearchOptions options;
        options.matchCase = IsDlgButtonChecked(IDC_FR_MATCH_CASE) == BST_CHECKED;
        options.wholeWord = IsDlgButtonChecked(IDC_FR_WHOLE_WORD) == BST_CHECKED;
        const int count = target->ReplaceAll(What(), std::wstring(with), options, IsDlgButtonChecked(IDC_FR_SELECTION) == BST_CHECKED);
        CString status;
        status.Format(count == 1 ? L"%d replacement." : L"%d replacements.", count);
        SetStatus(status);
    }

    void FindReplaceDlg::OnCancel()
    {
        ShowWindow(SW_HIDE);
        if (CompareWnd* target = m_target())
        {
            if (PaneView* pane = target->Pane(target->ActivePane()))
                pane->SetFocus();
        }
    }

    // ---- Go To Line ----

    GoToLineDlg::GoToLineDlg(int current, int count, CWnd* parent) : CDialog(IDD_GOTO_LINE, parent), line(current), m_count(count)
    {
    }

    BOOL GoToLineDlg::OnInitDialog()
    {
        CDialog::OnInitDialog();
        CString range;
        range.Format(L"Line number (1 - %d):", m_count);
        SetDlgItemText(IDC_GT_RANGE, range);
        SetDlgItemInt(IDC_GT_LINE, static_cast<UINT>(line));
        CEdit* edit = static_cast<CEdit*>(GetDlgItem(IDC_GT_LINE));
        edit->SetFocus();
        edit->SetSel(0, -1);
        return FALSE;
    }

    void GoToLineDlg::OnOK()
    {
        BOOL ok = FALSE;
        const UINT value = GetDlgItemInt(IDC_GT_LINE, &ok, FALSE);
        if (!ok || value < 1 || static_cast<int>(value) > m_count)
        {
            MessageBeep(MB_ICONEXCLAMATION);
            return;
        }
        line = static_cast<int>(value);
        CDialog::OnOK();
    }

    // ---- Statistics / About ----

    BOOL TextDlg::OnInitDialog()
    {
        CDialog::OnInitDialog();
        SetDlgItemText(GetDlgItem(IDC_STATS_TEXT) ? IDC_STATS_TEXT : IDC_ABOUT_VERSION, m_text);
        return TRUE;
    }
}
