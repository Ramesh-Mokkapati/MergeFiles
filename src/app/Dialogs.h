// Dialogs.h - New Comparison, Find and Replace, Go To Line, Statistics and About dialogs.
#pragma once

#include "framework.h"

#include <functional>

#include "resource.h"

namespace mf
{
    class CompareWnd;

    class NewComparisonDlg : public CDialog
    {
    public:
        NewComparisonDlg(bool threeWay, bool folders, CWnd* parent);
        bool threeWay;
        bool folders;
        std::vector<std::wstring> paths;  // left, [ancestor,] right

    protected:
        BOOL OnInitDialog() override;
        void OnOK() override;
        afx_msg void OnModeChanged();
        afx_msg void OnBrowse(UINT id);
        DECLARE_MESSAGE_MAP()

    private:
        void UpdateMode();
    };

    // Selection filter of a folder comparison.
    class SelectFilesDlg : public CDialog
    {
    public:
        explicit SelectFilesDlg(CWnd* parent) : CDialog(IDD_FOLDER_SELECT, parent)
        {
        }
        std::wstring pattern = L"*";
        bool identical = false, left = true, right = true, different = true;

    protected:
        BOOL OnInitDialog() override;
        void OnOK() override;
    };

    // Modeless; searches the active pane of whichever comparison is active when a button is pressed.
    class FindReplaceDlg : public CDialog
    {
    public:
        explicit FindReplaceDlg(std::function<CompareWnd*()> target);
        BOOL Create(CWnd* parent);
        void Show(bool replace, const CString& initialText);
        // F3 / Shift+F3 with the last search.
        bool FindAgain(bool backward);

    protected:
        BOOL OnInitDialog() override;
        void OnCancel() override;
        void OnOK() override;
        void PostNcDestroy() override;
        afx_msg void OnFindNext();
        afx_msg void OnFindPrev();
        afx_msg void OnReplace();
        afx_msg void OnReplaceAll();
        DECLARE_MESSAGE_MAP()

    private:
        bool Find(bool backward);
        void SetStatus(const CString& text);
        std::wstring What() const;

        std::function<CompareWnd*()> m_target;
        CString m_lastText;
        bool m_hasLast = false;
    };

    class GoToLineDlg : public CDialog
    {
    public:
        GoToLineDlg(int current, int count, CWnd* parent);
        int line;  // 1-based

    protected:
        BOOL OnInitDialog() override;
        void OnOK() override;

    private:
        int m_count;
    };

    class TextDlg : public CDialog
    {
    public:
        TextDlg(UINT id, const CString& text, CWnd* parent) : CDialog(id, parent), m_text(text)
        {
        }

    protected:
        BOOL OnInitDialog() override;

    private:
        CString m_text;
    };
}
