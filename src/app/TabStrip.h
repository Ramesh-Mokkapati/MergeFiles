// TabStrip.h - the strip of open comparisons under the toolbar. Click a tab to switch to it; the
// x button (or a middle click) closes it.
#pragma once

#include "framework.h"

namespace mf
{
    enum class TabKind
    {
        Text,    // file comparison
        Folder,  // folder comparison
        Image,   // image comparison
        Binary,  // byte comparison
        Patch,   // patch file
    };

    struct TabItem
    {
        CString title;
        CString tooltip;
        TabKind kind = TabKind::Text;
        bool modified = false;
    };

    class TabStripOwner
    {
    public:
        virtual void OnTabActivate(int index) = 0;
        virtual void OnTabClose(int index) = 0;

    protected:
        ~TabStripOwner() = default;
    };

    class TabStrip : public CControlBar
    {
    public:
        BOOL Create(CWnd* parent, TabStripOwner* owner);
        void SetTabs(const std::vector<TabItem>& tabs, int active);

        CSize CalcFixedLayout(BOOL stretch, BOOL horizontal) override;
        void OnUpdateCmdUI(CFrameWnd*, BOOL) override
        {
        }
        BOOL PreTranslateMessage(MSG* msg) override;

    protected:
        afx_msg void OnPaint();
        afx_msg BOOL OnEraseBkgnd(CDC* dc);
        afx_msg void OnSize(UINT type, int cx, int cy);
        afx_msg void OnMouseMove(UINT flags, CPoint point);
        afx_msg void OnMouseLeave();
        afx_msg void OnLButtonDown(UINT flags, CPoint point);
        afx_msg void OnLButtonUp(UINT flags, CPoint point);
        afx_msg void OnMButtonUp(UINT flags, CPoint point);
        DECLARE_MESSAGE_MAP()

    private:
        void Layout();
        int HitTest(CPoint point, bool* onClose = nullptr) const;

        TabStripOwner* m_owner = nullptr;
        std::vector<TabItem> m_tabs;
        std::vector<CRect> m_rects;
        std::vector<CRect> m_closeRects;
        int m_active = -1;
        int m_hot = -1;
        bool m_hotClose = false;
        bool m_tracking = false;
        CToolTipCtrl m_tips;
    };
}
