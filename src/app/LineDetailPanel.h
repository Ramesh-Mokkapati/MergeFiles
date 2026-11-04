// LineDetailPanel.h - optional panel under the panes that shows the current line of every file
// one above the other, wrapped to the full window width, with the changed characters highlighted.
// Useful for long lines that are hard to compare side by side.
#pragma once

#include "framework.h"

namespace mf
{
    class CompareWnd;

    class LineDetailPanel : public CWnd
    {
    public:
        explicit LineDetailPanel(CompareWnd& owner) : m_owner(owner)
        {
        }
        BOOL Create(CWnd* parent);
        int PreferredHeight(int panes) const;

    protected:
        afx_msg void OnPaint();
        afx_msg BOOL OnEraseBkgnd(CDC* dc);
        DECLARE_MESSAGE_MAP()

    private:
        int RowHeight() const;
        CompareWnd& m_owner;
    };
}
