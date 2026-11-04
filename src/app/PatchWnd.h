// PatchWnd.h - a patch file (one tab): its files and hunks with check boxes, the selected part of
// the patch shown in colour, and Apply, which applies the checked hunks to a folder and opens each
// patched file as a comparison (original on the left, patched and unsaved on the right) to review.
#pragma once

#include "framework.h"

#include "Patch.h"
#include "TabWindow.h"

namespace mf
{
    class PatchTextView : public CWnd
    {
    public:
        BOOL Create(CWnd* parent);
        void SetLines(std::vector<std::pair<wchar_t, std::wstring>> lines);  // (kind, text): '+', '-', '@', 'f' file header, ' '

    protected:
        afx_msg void OnPaint();
        afx_msg BOOL OnEraseBkgnd(CDC* dc);
        afx_msg void OnSize(UINT type, int cx, int cy);
        afx_msg void OnVScroll(UINT code, UINT pos, CScrollBar* bar);
        afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint point);
        DECLARE_MESSAGE_MAP()

    private:
        int RowHeight() const;
        void UpdateScrollBar();
        std::vector<std::pair<wchar_t, std::wstring>> m_lines;
        int m_top = 0;
    };

    class PatchWnd : public TabWindow
    {
    public:
        PatchWnd();
        ~PatchWnd() override;

        BOOL Create(CWnd* parent, UINT id);
        bool Open(const std::wstring& patchPath, CString& error);

        // ---- TabWindow ----
        TabItem GetTabItem() const override;
        bool IsModified() const override
        {
            return false;
        }
        bool QueryClose() override
        {
            return true;
        }
        void ApplySettings() override
        {
        }
        void UpdateMetrics() override;
        CString StatusText(int part) const override;
        const wchar_t* HelpTopic() const override
        {
            return L"patches";
        }
        CompareWnd* TextComparison() override
        {
            return nullptr;
        }
        BOOL OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* result) override;
        BOOL OnCommand(WPARAM wParam, LPARAM lParam) override;

    protected:
        afx_msg void OnPaint();
        afx_msg BOOL OnEraseBkgnd(CDC* dc);
        afx_msg void OnSize(UINT type, int cx, int cy);
        afx_msg void OnSetFocus(CWnd* oldWnd);
        afx_msg void OnReload();
        afx_msg void OnUpdateAlways(CCmdUI* ui);
        DECLARE_MESSAGE_MAP()

    private:
        struct Row
        {
            int file;
            int hunk;  // -1 for the file's own row
        };
        void FillList();
        void Layout();
        void ShowRow(int row);
        void Apply();
        void Browse();
        void CheckAll(bool check);
        bool IsChecked(int file, int hunk) const;
        std::wstring Folder() const;
        void NotifyFrame();

        std::wstring m_path;
        std::vector<FilePatch> m_patches;
        std::wstring m_parseError;
        std::vector<Row> m_rows;
        std::vector<std::vector<CString>> m_results;  // per file, per hunk
        CStatic m_folderLabel, m_stripLabel;
        CEdit m_folder, m_strip;
        CButton m_browse, m_reverse, m_apply, m_all, m_none;
        CListCtrl m_list;
        PatchTextView m_text;
        bool m_updatingChecks = false;
        CString m_lastSummary;
    };
}
