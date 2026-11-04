// FolderTreeView.h - the folder comparison as a tree list: one row per file or folder, with its
// status (tinted like changed blocks in file comparisons) and size and modification time per side.
#pragma once

#include "framework.h"

#include <functional>
#include <set>

#include "FolderCompare.h"

namespace mf
{
    class FolderCompareWnd;

    class FolderTreeView : public CWnd
    {
    public:
        explicit FolderTreeView(FolderCompareWnd& owner) : m_owner(owner)
        {
        }
        BOOL Create(CWnd* parent);

        // After a (re)comparison or a change of filters: rebuilds the rows, keeping expanded
        // folders and the selection (by relative path).
        void Rebuild();
        void ExpandAll(bool expand);
        std::vector<int> SelectedItems() const;  // item indices, in row order
        int FocusedItem() const;                 // -1 if none
        // Moves to the next/previous file that differs (or conflicts), opening folders on the way.
        bool GoToDifference(bool forward, bool conflictsOnly);
        bool CanGoToDifference(bool forward, bool conflictsOnly) const;
        // Selects the visible files for which `match` is true; returns how many.
        int SelectWhere(const std::function<bool(const FolderItem&)>& match);
        void UpdateMetrics();

    protected:
        afx_msg void OnPaint();
        afx_msg BOOL OnEraseBkgnd(CDC* dc);
        afx_msg void OnSize(UINT type, int cx, int cy);
        afx_msg void OnVScroll(UINT code, UINT pos, CScrollBar* bar);
        afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint point);
        afx_msg void OnLButtonDown(UINT flags, CPoint point);
        afx_msg void OnLButtonDblClk(UINT flags, CPoint point);
        afx_msg void OnRButtonDown(UINT flags, CPoint point);
        afx_msg void OnContextMenu(CWnd* wnd, CPoint point);
        afx_msg void OnKeyDown(UINT key, UINT repeat, UINT flags);
        afx_msg void OnSetFocus(CWnd* oldWnd);
        afx_msg void OnKillFocus(CWnd* newWnd);
        afx_msg UINT OnGetDlgCode();
        DECLARE_MESSAGE_MAP()

    private:
        struct Row
        {
            int item;
            int depth;
        };
        const FolderComparison* Comparison() const;
        void AddRows(int item, int depth);
        bool HasVisibleContent(int item) const;
        bool IsExpanded(int item) const;
        void SetExpanded(int item, bool expanded);
        void SetFocusRow(int row, bool extend, bool toggle);
        void EnsureVisible(int row);
        int RowAt(CPoint point) const;
        int VisibleRows() const;
        void UpdateScrollBar();
        void Columns(std::vector<int>& x) const;  // column edges
        static std::wstring Key(const FolderItem& item);

        FolderCompareWnd& m_owner;
        std::vector<Row> m_rows;
        std::set<std::wstring> m_expanded;  // lower-case relative paths
        std::set<std::wstring> m_selected;
        std::wstring m_focusKey;
        int m_focus = -1;   // row
        int m_anchor = -1;  // row
        int m_top = 0;
        int m_rowHeight = 20;
        int m_headerHeight = 24;
        bool m_hasFocus = false;
        bool m_initialized = false;  // the first build opens the folders that differ
    };
}
