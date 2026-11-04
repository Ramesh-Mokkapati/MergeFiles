// FolderCompareWnd.h - one folder comparison (one tab): root path bars, the folder tree, and an
// optional file comparison of the selected row below it. The comparison runs on a background
// thread (Stop cancels it); copying, deleting and automatic merging act on the selected rows.
#pragma once

#include "framework.h"

#include <atomic>
#include <mutex>
#include <thread>

#include "CompareWnd.h"
#include "FolderCompare.h"
#include "FolderTreeView.h"
#include "TabWindow.h"

#define WM_MF_FOLDERPROGRESS (WM_APP + 2)
#define WM_MF_FOLDERDONE (WM_APP + 3)

namespace mf
{
    class FolderCompareWnd : public TabWindow
    {
    public:
        FolderCompareWnd();
        ~FolderCompareWnd() override;

        BOOL Create(CWnd* parent, UINT id);
        // Starts comparing 2 or 3 folders.
        void Open(const std::vector<std::wstring>& roots);
        // Shows results saved in an archive (read-only until compared again with Start).
        void OpenArchive(std::unique_ptr<FolderComparison> comparison);

        // ---- TabWindow ----
        TabItem GetTabItem() const override;
        bool IsModified() const override;
        bool QueryClose() override;
        void ApplySettings() override;
        void UpdateMetrics() override;
        CString StatusText(int part) const override;
        CompareWnd* TextComparison() override;
        const wchar_t* HelpTopic() const override
        {
            for (int side = 0; m_comparison && side < m_comparison->SideCount(); ++side)
            {
                if (m_comparison->IsArchiveSide(side))
                    return L"archives";
            }
            return L"folder-comparison";
        }
        BOOL OnCmdMsg(UINT id, int code, void* extra, AFX_CMDHANDLERINFO* handlerInfo) override;

        // ---- used by FolderTreeView ----
        const FolderComparison* Comparison() const
        {
            return m_comparison.get();
        }
        bool IsItemVisible(const FolderItem& item) const;
        // Shows the item's files in the comparison below (or in a new tab).
        void OpenItem(int item, bool newTab);
        void ShowItemMenu(CPoint screenPoint);
        void OnSelectionChanged();

    protected:
        afx_msg void OnPaint();
        afx_msg BOOL OnEraseBkgnd(CDC* dc);
        afx_msg void OnSize(UINT type, int cx, int cy);
        afx_msg void OnSetFocus(CWnd* oldWnd);
        afx_msg void OnDestroy();
        afx_msg void OnTimer(UINT_PTR id);
        afx_msg void OnLButtonDown(UINT flags, CPoint point);
        afx_msg void OnLButtonUp(UINT flags, CPoint point);
        afx_msg void OnMouseMove(UINT flags, CPoint point);
        afx_msg BOOL OnSetCursor(CWnd* wnd, UINT hitTest, UINT message);
        afx_msg LRESULT OnProgress(WPARAM, LPARAM);
        afx_msg LRESULT OnDone(WPARAM, LPARAM);

        afx_msg void OnStart();
        afx_msg void OnStop();
        afx_msg void OnUpdateStart(CCmdUI* ui);
        afx_msg void OnUpdateStop(CCmdUI* ui);
        afx_msg void OnNextChange();
        afx_msg void OnPrevChange();
        afx_msg void OnNextConflict();
        afx_msg void OnPrevConflict();
        afx_msg void OnUpdateNextChange(CCmdUI* ui);
        afx_msg void OnUpdatePrevChange(CCmdUI* ui);
        afx_msg void OnUpdateNextConflict(CCmdUI* ui);
        afx_msg void OnUpdatePrevConflict(CCmdUI* ui);
        afx_msg void OnCopyToRight();
        afx_msg void OnCopyToLeft();
        afx_msg void OnUpdateCopy(CCmdUI* ui);
        afx_msg void OnDeleteLeft();
        afx_msg void OnDeleteRight();
        afx_msg void OnAutoMerge();
        afx_msg void OnUpdateAutoMerge(CCmdUI* ui);
        afx_msg void OnOpenItem();
        afx_msg void OnOpenItemTab();
        afx_msg void OnUpdateHasFile(CCmdUI* ui);
        afx_msg void OnExpandAll();
        afx_msg void OnCollapseAll();
        afx_msg void OnShowFilter(UINT id);
        afx_msg void OnUpdateShowFilter(CCmdUI* ui);
        afx_msg void OnSelectFiles();
        afx_msg void OnSelectAll();
        afx_msg void OnToggleSplit();
        afx_msg void OnUpdateSplit(CCmdUI* ui);
        afx_msg void OnStatistics();
        afx_msg void OnUpdateIdle(CCmdUI* ui);
        afx_msg void OnUpdateLayout(CCmdUI* ui);
        afx_msg void OnPrint();
        afx_msg void OnPrintPreview();
        afx_msg void OnReport();
        afx_msg void OnSaveArchive();
        DECLARE_MESSAGE_MAP()

    private:
        void StartCompare();
        void StopCompare();
        void Layout();
        CRect SplitterRect() const;
        void CopySelection(bool toRight);
        void DeleteSelection(int side);
        std::vector<int> TopSelection() const;  // selected items without those inside another selected folder
        bool Busy() const;
        void AfterChange(const std::vector<std::wstring>& changedPaths);
        void NotifyFrame();
        std::wstring SideName(int side) const;
        // A file inside an archive side, extracted to the temporary folder; "" on error.
        std::wstring ExtractItem(int item, int side, std::wstring& error);

        std::vector<std::wstring> m_roots;
        std::unique_ptr<FolderComparison> m_comparison;  // results shown
        std::unique_ptr<FolderComparison> m_running;     // being computed on m_thread
        std::thread m_thread;
        std::atomic<bool> m_cancel{ false };
        std::atomic<unsigned long long> m_lastPost{ 0 };
        mutable std::mutex m_progressLock;
        FolderProgress m_progress;
        bool m_stopped = false;
        bool m_snapshot = false;  // showing archived results: no file operations until compared again
        unsigned m_generation = 0;  // which run a "done" message belongs to
        FolderOptions m_options;

        FolderTreeView m_tree;
        std::unique_ptr<CompareWnd> m_file;
        int m_fileItem = -1;
        std::wstring m_fileKey;
        bool m_split = true;
        double m_splitRatio = 0.45;
        bool m_draggingSplitter = false;
        bool m_autoOpening = false;  // opening the selected row automatically (not on request)

        bool m_showIdentical = true;
        bool m_showLeft = true;
        bool m_showRight = true;
        bool m_showDifferent = true;
    };
}
