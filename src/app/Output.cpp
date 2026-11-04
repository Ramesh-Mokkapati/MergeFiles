// Output.cpp - report, page setup and bookmark comment dialogs; saving reports and archives; printing.
#include "Output.h"

#include "Archive.h"
#include "MergeFilesApp.h"
#include "Report.h"
#include "resource.h"

namespace mf
{
    const wchar_t* const kArchiveFilter = L"MergeFiles comparison archives (*.mfcmp)|*.mfcmp|All files (*.*)|*.*||";

    bool IsArchiveFileName(const std::wstring& path)
    {
        const size_t dot = path.find_last_of(L'.');
        return dot != std::wstring::npos && _wcsicmp(path.c_str() + dot, L".mfcmp") == 0;
    }

    std::wstring SuggestedName(const std::vector<std::wstring>& names)
    {
        std::wstring result;
        for (const std::wstring& name : names)
        {
            std::wstring leaf = name.substr(name.find_last_of(L"\\/") == std::wstring::npos ? 0 : name.find_last_of(L"\\/") + 1);
            const size_t dot = leaf.find_last_of(L'.');
            if (dot != std::wstring::npos && dot > 0)
                leaf.erase(dot);
            if (leaf.empty())
                continue;
            result += (result.empty() ? L"" : L"_vs_") + leaf;
        }
        for (wchar_t& c : result)
        {
            if (wcschr(L"<>:\"/\\|?* ()", c) || c < 0x20)
                c = L'_';
        }
        return result.empty() ? L"comparison" : result;
    }

    // ---- report dialog ----

    namespace
    {
        class ReportDlg : public CDialog
        {
        public:
            ReportDlg(bool folders, CWnd* parent) : CDialog(IDD_REPORT, parent), m_folders(folders)
            {
            }
            ReportSettings settings;

        protected:
            BOOL OnInitDialog() override
            {
                CDialog::OnInitDialog();
                if (m_folders)
                {
                    SetWindowTextW(L"Create Folder Report");
                    settings.format = settings.format == 2 ? 2 : 0;  // HTML or XML
                    GetDlgItem(IDC_RP_SLIDES)->EnableWindow(FALSE);
                    GetDlgItem(IDC_RP_DIFF)->EnableWindow(FALSE);
                }
                CheckRadioButton(IDC_RP_HTML, IDC_RP_DIFF, IDC_RP_HTML + settings.format);
                CheckRadioButton(IDC_RP_WHOLE, IDC_RP_CONTEXT, settings.wholeFiles ? IDC_RP_WHOLE : IDC_RP_CONTEXT);
                SetDlgItemInt(IDC_RP_LINES, static_cast<UINT>(settings.context), FALSE);
                CheckDlgButton(IDC_RP_FILES, settings.fileReports ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(IDC_RP_IDENTICAL, settings.identical ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(IDC_RP_OPEN, settings.openWhenSaved ? BST_CHECKED : BST_UNCHECKED);
                UpdateControls();
                return TRUE;
            }

            BOOL OnCommand(WPARAM wParam, LPARAM lParam) override
            {
                const BOOL result = CDialog::OnCommand(wParam, lParam);
                if (HIWORD(wParam) == BN_CLICKED)
                    UpdateControls();
                return result;
            }

            void OnOK() override
            {
                settings.format = GetCheckedRadioButton(IDC_RP_HTML, IDC_RP_DIFF) - IDC_RP_HTML;
                settings.wholeFiles = IsDlgButtonChecked(IDC_RP_WHOLE) == BST_CHECKED;
                BOOL ok = FALSE;
                const UINT lines = GetDlgItemInt(IDC_RP_LINES, &ok, FALSE);
                settings.context = ok ? static_cast<int>((std::min)(lines, 1000u)) : 3;
                settings.fileReports = IsDlgButtonChecked(IDC_RP_FILES) == BST_CHECKED;
                settings.identical = IsDlgButtonChecked(IDC_RP_IDENTICAL) == BST_CHECKED;
                settings.openWhenSaved = IsDlgButtonChecked(IDC_RP_OPEN) == BST_CHECKED;
                CDialog::OnOK();
            }

        private:
            void UpdateControls()
            {
                const int format = GetCheckedRadioButton(IDC_RP_HTML, IDC_RP_DIFF) - IDC_RP_HTML;
                const bool html = format == 0 || format == 1;
                const bool files = !m_folders || IsDlgButtonChecked(IDC_RP_FILES) == BST_CHECKED;
                // A unified diff always has context; a folder report's file sections use the context setting.
                GetDlgItem(IDC_RP_WHOLE)->EnableWindow(format != 3 && (!m_folders || (html && files)));
                GetDlgItem(IDC_RP_CONTEXT)->EnableWindow(!m_folders || (html && files));
                GetDlgItem(IDC_RP_LINES)->EnableWindow(!m_folders || (html && files));
                if (format == 3 && IsDlgButtonChecked(IDC_RP_WHOLE) == BST_CHECKED)
                    CheckRadioButton(IDC_RP_WHOLE, IDC_RP_CONTEXT, IDC_RP_CONTEXT);
                GetDlgItem(IDC_RP_FILES)->EnableWindow(m_folders && html);
                GetDlgItem(IDC_RP_IDENTICAL)->EnableWindow(m_folders);
            }

            bool m_folders;
        };
    }

    void CreateReport(const Comparison* files, const FolderComparison* folders, CWnd* parent)
    {
        ReportDlg dialog(folders != nullptr, parent);
        dialog.settings = App().reportSettings;
        if (dialog.DoModal() != IDOK)
            return;
        const ReportSettings settings = dialog.settings;
        App().reportSettings = settings;

        static const wchar_t* extensions[] = { L"html", L"html", L"xml", L"diff" };
        static const wchar_t* filters[] = {
            L"HTML files (*.html)|*.html|All files (*.*)|*.*||",
            L"HTML files (*.html)|*.html|All files (*.*)|*.*||",
            L"XML files (*.xml)|*.xml|All files (*.*)|*.*||",
            L"Patch files (*.diff;*.patch)|*.diff;*.patch|All files (*.*)|*.*||",
        };
        std::vector<std::wstring> names;
        if (files)
        {
            for (int pane = 0; pane < files->PaneCount(); ++pane)
                names.push_back(files->Buffer(pane).title);
        }
        else
        {
            for (int side = 0; side < folders->SideCount(); ++side)
                names.push_back(folders->Root(side));
        }
        const std::wstring suggested = SuggestedName(names) + L"." + extensions[settings.format];
        CFileDialog save(FALSE, extensions[settings.format], suggested.c_str(), OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY, filters[settings.format], parent);
        if (save.DoModal() != IDOK)
            return;
        const std::wstring path(save.GetPathName());

        CWaitCursor wait;
        std::wstring text;
        if (files)
        {
            ReportOptions options;
            options.format = static_cast<ReportFormat>(settings.format);
            options.context = settings.wholeFiles && settings.format != 3 ? -1 : settings.context;
            text = settings.format == 3 ? UnifiedDiffText(*files, options.context) : MakeReport(*files, options);
        }
        else
        {
            FolderReportOptions options;
            options.xml = settings.format == 2;
            options.onlyDifferences = !settings.identical;
            options.includeFileReports = settings.fileReports;
            options.context = settings.wholeFiles ? -1 : settings.context;
            text = MakeFolderReport(*folders, options);
        }
        std::wstring error;
        if (!WriteArchiveFile(path, text, error))
        {
            AfxMessageBox((L"Cannot save the report:\n" + error).c_str(), MB_ICONERROR);
            return;
        }
        if (CFrameWnd* frame = parent ? parent->GetParentFrame() : nullptr)
            frame->SetMessageText((L"Report saved as " + path).c_str());
        if (settings.openWhenSaved)
            ShellExecuteW(parent ? parent->GetSafeHwnd() : nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    void SaveArchiveAs(const std::wstring& text, const std::wstring& suggestedName, CWnd* parent)
    {
        const std::wstring name = suggestedName + L".mfcmp";
        CFileDialog save(FALSE, L"mfcmp", name.c_str(), OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY, kArchiveFilter, parent);
        if (save.DoModal() != IDOK)
            return;
        std::wstring error;
        if (!WriteArchiveFile(std::wstring(save.GetPathName()), text, error))
        {
            AfxMessageBox((L"Cannot save the archive:\n" + error).c_str(), MB_ICONERROR);
            return;
        }
        if (CFrameWnd* frame = parent ? parent->GetParentFrame() : nullptr)
            frame->SetMessageText(L"Comparison archive saved as " + save.GetPathName());
    }

    // ---- page setup ----

    namespace
    {
        class PageSetupDlg : public CDialog
        {
        public:
            explicit PageSetupDlg(CWnd* parent) : CDialog(IDD_PAGE_SETUP, parent)
            {
            }
            PageSettings settings;

        protected:
            BOOL OnInitDialog() override
            {
                CDialog::OnInitDialog();
                SetDlgItemTextW(IDC_PS_HEADER, settings.header.c_str());
                SetDlgItemTextW(IDC_PS_FOOTER, settings.footer.c_str());
                CheckDlgButton(IDC_PS_LANDSCAPE, settings.landscape ? BST_CHECKED : BST_UNCHECKED);
                CheckDlgButton(IDC_PS_CHANGED, settings.changedPagesOnly ? BST_CHECKED : BST_UNCHECKED);
                SetDlgItemInt(IDC_PS_FONT, static_cast<UINT>(settings.fontPoints), FALSE);
                return TRUE;
            }

            void OnOK() override
            {
                CString text;
                GetDlgItemTextW(IDC_PS_HEADER, text);
                settings.header = text;
                GetDlgItemTextW(IDC_PS_FOOTER, text);
                settings.footer = text;
                settings.landscape = IsDlgButtonChecked(IDC_PS_LANDSCAPE) == BST_CHECKED;
                settings.changedPagesOnly = IsDlgButtonChecked(IDC_PS_CHANGED) == BST_CHECKED;
                BOOL ok = FALSE;
                const UINT points = GetDlgItemInt(IDC_PS_FONT, &ok, FALSE);
                if (!ok || points < 5 || points > 24)
                {
                    AfxMessageBox(L"Enter a font size from 5 to 24 points.", MB_ICONEXCLAMATION);
                    GotoDlgCtrl(GetDlgItem(IDC_PS_FONT));
                    return;
                }
                settings.fontPoints = static_cast<int>(points);
                CDialog::OnOK();
            }
        };

        class CommentDlg : public CDialog
        {
        public:
            CommentDlg(const CString& label, CWnd* parent) : CDialog(IDD_COMMENT, parent), m_label(label)
            {
            }
            std::wstring comment;

        protected:
            BOOL OnInitDialog() override
            {
                CDialog::OnInitDialog();
                SetDlgItemTextW(IDC_CM_LABEL, m_label);
                // The edit box shows line breaks as CR LF.
                CString text(comment.c_str());
                text.Replace(L"\n", L"\r\n");
                SetDlgItemTextW(IDC_CM_TEXT, text);
                GotoDlgCtrl(GetDlgItem(IDC_CM_TEXT));
                static_cast<CEdit*>(GetDlgItem(IDC_CM_TEXT))->SetSel(0, -1);
                return FALSE;
            }

            void OnOK() override
            {
                CString text;
                GetDlgItemTextW(IDC_CM_TEXT, text);
                text.Replace(L"\r\n", L"\n");
                text.Trim();
                comment = text;
                CDialog::OnOK();
            }

        private:
            CString m_label;
        };
    }

    void EditPageSetup(CWnd* parent)
    {
        PageSetupDlg dialog(parent);
        dialog.settings = App().pageSettings;
        if (dialog.DoModal() == IDOK)
            App().pageSettings = dialog.settings;
    }

    void PrintComparison(const PrintModel& model, bool preview, CWnd* parent)
    {
        if (preview)
            ShowPrintPreview(model, App().pageSettings, parent);
        else
            PrintModelToPrinter(model, App().pageSettings, parent);
    }

    bool EditBookmarkComment(const CString& label, std::wstring& comment, CWnd* parent)
    {
        CommentDlg dialog(label, parent);
        dialog.comment = comment;
        if (dialog.DoModal() != IDOK)
            return false;
        comment = dialog.comment;
        return true;
    }
}
