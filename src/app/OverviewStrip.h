// OverviewStrip.h - a thin map of a whole file at the edge of the comparison: every change is a
// coloured mark and the visible part is outlined. Clicking (or dragging) jumps there.
#pragma once

#include "framework.h"

namespace mf
{
    class CompareWnd;

    class OverviewStrip : public CWnd
    {
    public:
        OverviewStrip(CompareWnd& owner, int pane) : m_owner(owner), m_pane(pane)
        {
        }
        BOOL Create(CWnd* parent);
        void SetPane(int pane)
        {
            m_pane = pane;
        }

    protected:
        afx_msg void OnPaint();
        afx_msg BOOL OnEraseBkgnd(CDC* dc);
        afx_msg void OnLButtonDown(UINT flags, CPoint point);
        afx_msg void OnMouseMove(UINT flags, CPoint point);
        afx_msg void OnLButtonUp(UINT flags, CPoint point);
        DECLARE_MESSAGE_MAP()

    private:
        void JumpTo(CPoint point);

        CompareWnd& m_owner;
        int m_pane;
        bool m_dragging = false;
    };
}
