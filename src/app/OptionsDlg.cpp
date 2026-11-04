// OptionsDlg.cpp - the three pages of the Options dialog.
#include "OptionsDlg.h"

#include "MergeFilesApp.h"
#include "resource.h"

namespace mf
{
    namespace
    {
        CString FormatNumber(double value)
        {
            CString text;
            text.Format(L"%.15g", value);
            return text;
        }

        bool ReadNumber(CWnd* page, UINT id, double& value)
        {
            CString text;
            page->GetDlgItemText(id, text);
            text.Trim();
            if (text.IsEmpty())
            {
                value = 0;
                return true;
            }
            wchar_t* end = nullptr;
            value = wcstod(text, &end);
            if (*end != 0 || value < 0)
            {
                AfxMessageBox(L"Enter a number that is zero or more.", MB_ICONEXCLAMATION);
                page->GetDlgItem(id)->SetFocus();
                return false;
            }
            return true;
        }

        std::vector<std::wstring> ReadLines(CWnd* page, UINT id)
        {
            CString text;
            page->GetDlgItemText(id, text);
            std::vector<std::wstring> lines = SplitList(text);
            for (std::wstring& line : lines)
            {
                while (!line.empty() && line.back() == L' ')
                    line.pop_back();
            }
            lines.erase(std::remove(lines.begin(), lines.end(), std::wstring()), lines.end());
            return lines;
        }

        CString JoinLinesCrLf(const std::vector<std::wstring>& items)
        {
            CString text;
            for (const std::wstring& item : items)
            {
                if (!text.IsEmpty())
                    text += L"\r\n";
                text += item.c_str();
            }
            return text;
        }

        // Reports the first invalid regular expression; false if there is one.
        bool CheckAll(CWnd* page, UINT id, const std::vector<std::wstring>& patterns)
        {
            for (const std::wstring& pattern : patterns)
            {
                const std::wstring error = CheckRegex(pattern);
                if (!error.empty())
                {
                    CString message;
                    message.Format(L"This regular expression is not valid:\n\n%s\n\n%s", pattern.c_str(), error.c_str());
                    AfxMessageBox(message, MB_ICONEXCLAMATION);
                    page->GetDlgItem(id)->SetFocus();
                    return false;
                }
            }
            return true;
        }

        class ComparePage : public CPropertyPage
        {
        public:
            explicit ComparePage(OptionsData& data) : CPropertyPage(IDD_OPT_COMPARE), m_data(data)
            {
            }

        protected:
            BOOL OnInitDialog() override
            {
                CPropertyPage::OnInitDialog();
                const CompareOptions& o = m_data.compare;
                CheckRadioButton(IDC_OC_WS_COMPARE, IDC_OC_WS_ALL,
                    o.whitespace == WhitespaceMode::Compare ? IDC_OC_WS_COMPARE : (o.whitespace == WhitespaceMode::IgnoreChanges ? IDC_OC_WS_CHANGES : IDC_OC_WS_ALL));
                CheckDlgButton(IDC_OC_CASE, o.ignoreCase);
                CheckDlgButton(IDC_OC_EOL, o.ignoreEol);
                CheckDlgButton(IDC_OC_BLANK, o.ignoreBlankLines);
                SetDlgItemText(IDC_OC_ABS, FormatNumber(o.absoluteTolerance));
                SetDlgItemText(IDC_OC_REL, FormatNumber(o.relativeTolerance));
                return TRUE;
            }

            BOOL OnKillActive() override
            {
                double value = 0;
                return ReadNumber(this, IDC_OC_ABS, value) && ReadNumber(this, IDC_OC_REL, value) && CPropertyPage::OnKillActive();
            }

            void OnOK() override
            {
                CompareOptions& o = m_data.compare;
                o.whitespace = IsDlgButtonChecked(IDC_OC_WS_CHANGES) ? WhitespaceMode::IgnoreChanges
                    : IsDlgButtonChecked(IDC_OC_WS_ALL)              ? WhitespaceMode::IgnoreAll
                                                                     : WhitespaceMode::Compare;
                o.ignoreCase = IsDlgButtonChecked(IDC_OC_CASE) != 0;
                o.ignoreEol = IsDlgButtonChecked(IDC_OC_EOL) != 0;
                o.ignoreBlankLines = IsDlgButtonChecked(IDC_OC_BLANK) != 0;
                ReadNumber(this, IDC_OC_ABS, o.absoluteTolerance);
                ReadNumber(this, IDC_OC_REL, o.relativeTolerance);
                CPropertyPage::OnOK();
            }

        private:
            OptionsData& m_data;
        };

        class RulesPage : public CPropertyPage
        {
        public:
            explicit RulesPage(OptionsData& data) : CPropertyPage(IDD_OPT_RULES), m_data(data)
            {
            }

        protected:
            BOOL OnInitDialog() override
            {
                CPropertyPage::OnInitDialog();
                const CompareOptions& o = m_data.compare;
                SetDlgItemText(IDC_OR_LINE, JoinLinesCrLf(o.lineExpressions));
                std::vector<std::wstring> begins, ends;
                for (const auto& block : o.blockExpressions)
                {
                    begins.push_back(block.first);
                    ends.push_back(block.second);
                }
                SetDlgItemText(IDC_OR_BEGIN, JoinLinesCrLf(begins));
                SetDlgItemText(IDC_OR_END, JoinLinesCrLf(ends));
                SetDlgItemText(IDC_OR_PAIRING, o.linePairingRule.c_str());
                // Fixed-pitch text makes expressions easier to read.
                for (UINT id : { IDC_OR_LINE, IDC_OR_BEGIN, IDC_OR_END, IDC_OR_PAIRING })
                    GetDlgItem(id)->SetFont(&GetFonts().code);
                return TRUE;
            }

            BOOL OnKillActive() override
            {
                if (!CheckAll(this, IDC_OR_LINE, ReadLines(this, IDC_OR_LINE)) || !CheckAll(this, IDC_OR_BEGIN, ReadLines(this, IDC_OR_BEGIN)) ||
                    !CheckAll(this, IDC_OR_END, ReadLines(this, IDC_OR_END)))
                    return FALSE;
                if (ReadLines(this, IDC_OR_BEGIN).size() != ReadLines(this, IDC_OR_END).size())
                {
                    AfxMessageBox(L"Give every Begin expression an End expression on the same line.", MB_ICONEXCLAMATION);
                    GetDlgItem(IDC_OR_END)->SetFocus();
                    return FALSE;
                }
                CString pairing;
                GetDlgItemText(IDC_OR_PAIRING, pairing);
                pairing.Trim();
                if (!pairing.IsEmpty() && !CheckAll(this, IDC_OR_PAIRING, { std::wstring(pairing) }))
                    return FALSE;
                return CPropertyPage::OnKillActive();
            }

            void OnOK() override
            {
                CompareOptions& o = m_data.compare;
                o.lineExpressions = ReadLines(this, IDC_OR_LINE);
                const std::vector<std::wstring> begins = ReadLines(this, IDC_OR_BEGIN);
                const std::vector<std::wstring> ends = ReadLines(this, IDC_OR_END);
                o.blockExpressions.clear();
                for (size_t i = 0; i < begins.size() && i < ends.size(); ++i)
                    o.blockExpressions.emplace_back(begins[i], ends[i]);
                CString pairing;
                GetDlgItemText(IDC_OR_PAIRING, pairing);
                pairing.Trim();
                o.linePairingRule = std::wstring(pairing);
                CPropertyPage::OnOK();
            }

        private:
            OptionsData& m_data;
        };

        class DisplayPage : public CPropertyPage
        {
        public:
            explicit DisplayPage(OptionsData& data) : CPropertyPage(IDD_OPT_DISPLAY), m_data(data)
            {
            }

        protected:
            BOOL OnInitDialog() override
            {
                CPropertyPage::OnInitDialog();
                const ViewSettings& v = m_data.view;
                SetDlgItemInt(IDC_OD_TAB, static_cast<UINT>(v.tabSize));
                CheckDlgButton(IDC_OD_WRAP, v.wordWrap);
                CheckDlgButton(IDC_OD_PADDING, v.verticalPadding);
                CheckDlgButton(IDC_OD_DETAIL, v.lineDetail);
                CheckDlgButton(IDC_OD_SYNTAX, v.syntax);
                CheckDlgButton(IDC_OD_WHITESPACE, v.showWhitespace);
                CListBox* list = static_cast<CListBox*>(GetDlgItem(IDC_OD_COLORS));
                for (const ThemeColor& color : ThemeColors())
                    list->AddString(color.name);
                list->SetCurSel(0);
                ShowFontName();
                return TRUE;
            }

            BOOL OnKillActive() override
            {
                BOOL ok = FALSE;
                const UINT tab = GetDlgItemInt(IDC_OD_TAB, &ok, FALSE);
                if (!ok || tab < 1 || tab > 16)
                {
                    AfxMessageBox(L"The tab size must be between 1 and 16.", MB_ICONEXCLAMATION);
                    GetDlgItem(IDC_OD_TAB)->SetFocus();
                    return FALSE;
                }
                return CPropertyPage::OnKillActive();
            }

            void OnOK() override
            {
                ViewSettings& v = m_data.view;
                v.tabSize = static_cast<int>(GetDlgItemInt(IDC_OD_TAB, nullptr, FALSE));
                v.wordWrap = IsDlgButtonChecked(IDC_OD_WRAP) != 0;
                v.verticalPadding = IsDlgButtonChecked(IDC_OD_PADDING) != 0;
                v.lineDetail = IsDlgButtonChecked(IDC_OD_DETAIL) != 0;
                v.syntax = IsDlgButtonChecked(IDC_OD_SYNTAX) != 0;
                v.showWhitespace = IsDlgButtonChecked(IDC_OD_WHITESPACE) != 0;
                CPropertyPage::OnOK();
            }

            afx_msg void OnFont()
            {
                LOGFONTW lf = {};
                CClientDC dc(this);
                lf.lfHeight = -MulDiv(m_data.fontSize, dc.GetDeviceCaps(LOGPIXELSY), 72);
                wcscpy_s(lf.lfFaceName, m_data.fontFace.empty() ? L"Cascadia Mono" : m_data.fontFace.c_str());
                CFontDialog dialog(&lf, CF_SCREENFONTS | CF_FIXEDPITCHONLY | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS, nullptr, this);
                if (dialog.DoModal() != IDOK)
                    return;
                m_data.fontFace = std::wstring(dialog.GetFaceName());
                m_data.fontSize = (std::max)(6, dialog.GetSize() / 10);
                ShowFontName();
            }

            afx_msg void OnColorSelected()
            {
                GetDlgItem(IDC_OD_SWATCH)->Invalidate();
            }

            afx_msg void OnChangeColor()
            {
                const int index = static_cast<CListBox*>(GetDlgItem(IDC_OD_COLORS))->GetCurSel();
                if (index < 0)
                    return;
                COLORREF& color = m_data.theme.*ThemeColors()[static_cast<size_t>(index)].member;
                CColorDialog dialog(color, CC_FULLOPEN, this);
                if (dialog.DoModal() != IDOK)
                    return;
                color = dialog.GetColor();
                GetDlgItem(IDC_OD_SWATCH)->Invalidate();
            }

            afx_msg void OnResetColors()
            {
                const Theme defaults;
                for (const ThemeColor& color : ThemeColors())
                    m_data.theme.*color.member = defaults.*color.member;
                GetDlgItem(IDC_OD_SWATCH)->Invalidate();
            }

            afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* wnd, UINT type)
            {
                if (wnd->GetDlgCtrlID() == IDC_OD_SWATCH)
                {
                    const int index = static_cast<CListBox*>(GetDlgItem(IDC_OD_COLORS))->GetCurSel();
                    const COLORREF color = index >= 0 ? m_data.theme.*ThemeColors()[static_cast<size_t>(index)].member : RGB(255, 255, 255);
                    m_swatch.DeleteObject();
                    m_swatch.CreateSolidBrush(color);
                    dc->SetBkColor(color);
                    return m_swatch;
                }
                return CPropertyPage::OnCtlColor(dc, wnd, type);
            }

            DECLARE_MESSAGE_MAP()

        private:
            void ShowFontName()
            {
                CString name;
                name.Format(L"%s, %d pt", m_data.fontFace.empty() ? L"Cascadia Mono / Consolas (default)" : m_data.fontFace.c_str(), m_data.fontSize);
                SetDlgItemText(IDC_OD_FONT_NAME, name);
            }

            OptionsData& m_data;
            CBrush m_swatch;
        };

        class FoldersPage : public CPropertyPage
        {
        public:
            explicit FoldersPage(OptionsData& data) : CPropertyPage(IDD_OPT_FOLDERS), m_data(data)
            {
            }

        protected:
            BOOL OnInitDialog() override
            {
                CPropertyPage::OnInitDialog();
                const FolderOptions& f = m_data.folders;
                CheckRadioButton(IDC_OF_QUICK, IDC_OF_THOROUGH,
                    f.mode == FolderCompareMode::Quick ? IDC_OF_QUICK : (f.mode == FolderCompareMode::Thorough ? IDC_OF_THOROUGH : IDC_OF_BINARY));
                SetDlgItemInt(IDC_OF_LIMIT, static_cast<UINT>(f.thoroughLimit / (1024 * 1024)));
                SetDlgItemText(IDC_OF_INCLUDE, f.include.c_str());
                SetDlgItemText(IDC_OF_EXCLUDE, f.exclude.c_str());
                SetDlgItemInt(IDC_OF_IMAGE_TOLERANCE, static_cast<UINT>(m_data.imageTolerance));
                return TRUE;
            }

            BOOL OnKillActive() override
            {
                BOOL ok = FALSE;
                const UINT limit = GetDlgItemInt(IDC_OF_LIMIT, &ok, FALSE);
                if (!ok || limit < 1 || limit > 4096)
                {
                    AfxMessageBox(L"The size limit must be between 1 and 4096 MB.", MB_ICONEXCLAMATION);
                    GetDlgItem(IDC_OF_LIMIT)->SetFocus();
                    return FALSE;
                }
                const UINT tolerance = GetDlgItemInt(IDC_OF_IMAGE_TOLERANCE, &ok, FALSE);
                if (!ok || tolerance > 255)
                {
                    AfxMessageBox(L"The image colour tolerance must be between 0 and 255.", MB_ICONEXCLAMATION);
                    GetDlgItem(IDC_OF_IMAGE_TOLERANCE)->SetFocus();
                    return FALSE;
                }
                return CPropertyPage::OnKillActive();
            }

            void OnOK() override
            {
                FolderOptions& f = m_data.folders;
                f.mode = IsDlgButtonChecked(IDC_OF_QUICK) ? FolderCompareMode::Quick
                    : IsDlgButtonChecked(IDC_OF_THOROUGH) ? FolderCompareMode::Thorough
                                                          : FolderCompareMode::Binary;
                f.thoroughLimit = static_cast<uint64_t>(GetDlgItemInt(IDC_OF_LIMIT, nullptr, FALSE)) * 1024 * 1024;
                CString text;
                GetDlgItemText(IDC_OF_INCLUDE, text);
                text.Trim();
                f.include = std::wstring(text);
                GetDlgItemText(IDC_OF_EXCLUDE, text);
                text.Trim();
                f.exclude = std::wstring(text);
                m_data.imageTolerance = static_cast<int>(GetDlgItemInt(IDC_OF_IMAGE_TOLERANCE, nullptr, FALSE));
                CPropertyPage::OnOK();
            }

        private:
            OptionsData& m_data;
        };

        BEGIN_MESSAGE_MAP(DisplayPage, CPropertyPage)
            ON_BN_CLICKED(IDC_OD_FONT, &DisplayPage::OnFont)
            ON_LBN_SELCHANGE(IDC_OD_COLORS, &DisplayPage::OnColorSelected)
            ON_LBN_DBLCLK(IDC_OD_COLORS, &DisplayPage::OnChangeColor)
            ON_BN_CLICKED(IDC_OD_COLOR_CHANGE, &DisplayPage::OnChangeColor)
            ON_BN_CLICKED(IDC_OD_COLOR_RESET, &DisplayPage::OnResetColors)
            ON_WM_CTLCOLOR()
        END_MESSAGE_MAP()
    }

    bool ShowOptionsDialog(CWnd* parent, OptionsData& data)
    {
        OptionsData edited = data;
        CPropertySheet sheet(L"Options", parent);
        sheet.m_psh.dwFlags |= PSH_NOAPPLYNOW;
        ComparePage compare(edited);
        RulesPage rules(edited);
        DisplayPage display(edited);
        FoldersPage folders(edited);
        sheet.AddPage(&compare);
        sheet.AddPage(&rules);
        sheet.AddPage(&display);
        sheet.AddPage(&folders);
        if (sheet.DoModal() != IDOK)
            return false;
        data = edited;
        return true;
    }
}
