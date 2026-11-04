// BinaryCompareWnd.h - byte-level comparison of two files (one tab): two hex panes side by side
// (offset, 16 bytes, characters), differing bytes highlighted, scrolling kept in step.
#pragma once

#include "framework.h"

#include "BinaryDiff.h"
#include "TabWindow.h"

namespace mf
{
    class BinaryCompareWnd;

    class HexPane : public CWnd
    {
    public:
        HexPane(BinaryCompareWnd& owner, int side) : m_owner(owner), m_side(side)
        {
        }
        BOOL Create(CWnd* parent);
        int TopRow() const
        {
            return m_top;
        }
        int VisibleRows() const;
        void SetTopRow(int row, bool sync);
        void UpdateMetrics();
        void Refresh();
        int64_t Caret() const
        {
            return m_caret;
        }
        void SetCaret(int64_t offset);

        static const int kBytesPerRow = 16;

    protected:
        afx_msg void OnPaint();
        afx_msg BOOL OnEraseBkgnd(CDC* dc);
        afx_msg void OnSize(UINT type, int cx, int cy);
        afx_msg void OnVScroll(UINT code, UINT pos, CScrollBar* bar);
        afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint point);
        afx_msg void OnLButtonDown(UINT flags, CPoint point);
        afx_msg void OnKeyDown(UINT key, UINT repeat, UINT flags);
        afx_msg UINT OnGetDlgCode();
        DECLARE_MESSAGE_MAP()

    private:
        int Rows() const;
        void UpdateScrollBar();
        BinaryCompareWnd& m_owner;
        int m_side;
        int m_top = 0;
        int64_t m_caret = -1;
        int m_rowHeight = 16;
        int m_charWidth = 8;
    };

    class BinaryCompareWnd : public TabWindow
    {
    public:
        BinaryCompareWnd();
        ~BinaryCompareWnd() override;

        BOOL Create(CWnd* parent, UINT id);
        bool Open(const std::wstring& left, const std::wstring& right, CString& error);

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
            return L"binary";
        }
        CompareWnd* TextComparison() override
        {
            return nullptr;
        }
        void OnShown() override;

        // ---- used by the panes ----
        const std::string& Data(int side) const
        {
            return m_data[side];
        }
        // 0 = equal, 1 = changed, 2 = only on this side (inserted/removed)
        uint8_t ByteState(int side, int64_t offset) const;
        int CurrentDifference() const
        {
            return m_current;
        }
        bool InCurrentDifference(int side, int64_t offset) const;
        void SyncScroll(int fromSide);
        void OnCaretMoved(int side);

    protected:
        afx_msg void OnPaint();
        afx_msg BOOL OnEraseBkgnd(CDC* dc);
        afx_msg void OnSize(UINT type, int cx, int cy);
        afx_msg void OnSetFocus(CWnd* oldWnd);
        afx_msg void OnNextChange();
        afx_msg void OnPrevChange();
        afx_msg void OnUpdateNextChange(CCmdUI* ui);
        afx_msg void OnUpdatePrevChange(CCmdUI* ui);
        afx_msg void OnReload();
        afx_msg void OnStatistics();
        afx_msg void OnUpdateAlways(CCmdUI* ui);
        DECLARE_MESSAGE_MAP()

    private:
        void Compare();
        void Layout();
        int FindDifference(bool forward) const;
        void GoToDifference(int index);
        double MapOffset(int from, double offset, int to) const;
        void NotifyFrame();

        std::wstring m_paths[2];
        std::string m_data[2];
        std::vector<uint8_t> m_state[2];
        BinaryDiff m_diff;
        int m_current = -1;
        std::unique_ptr<HexPane> m_panes[2];
        int m_activeSide = 0;
        bool m_syncing = false;
    };
}
