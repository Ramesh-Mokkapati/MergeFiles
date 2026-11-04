// LinkGutter.h - the strip between two panes: curved linking lines join each changed block on the
// left to its counterpart on the right, so no padding lines are needed to keep them side by side.
#pragma once

#include "framework.h"

namespace mf
{
    class CompareWnd;

    class LinkGutter : public CWnd
    {
    public:
        LinkGutter(CompareWnd& owner, int leftPane) : m_owner(owner), m_left(leftPane)
        {
        }
        BOOL Create(CWnd* parent);
        void SetLeftPane(int pane)
        {
            m_left = pane;
        }

    protected:
        afx_msg void OnPaint();
        afx_msg BOOL OnEraseBkgnd(CDC* dc);
        afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint point);
        DECLARE_MESSAGE_MAP()

    private:
        CompareWnd& m_owner;
        int m_left;  // the pane on the left; the right one is m_left + 1
    };
}
