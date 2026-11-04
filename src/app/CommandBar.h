// CommandBar.h - the toolbar: icon buttons in labelled groups (New Comparison, Start/Stop, Changes,
// ...), custom drawn, with tooltips and drop-down menus. Enabled/checked states come from the
// normal MFC ON_UPDATE_COMMAND_UI handlers.
#pragma once

#include "framework.h"

namespace mf
{
    class CommandBar : public CControlBar
    {
    public:
        BOOL Create(CWnd* parent);

        CSize CalcFixedLayout(BOOL stretch, BOOL horizontal) override;
        void OnUpdateCmdUI(CFrameWnd* target, BOOL disableIfNoHandler) override;
        BOOL PreTranslateMessage(MSG* msg) override;

        enum class Icon
        {
            Glyph,      // a Segoe icon font glyph
            NewTwo,     // document with a "2" badge
            NewThree,   // document with a "3" badge
            LayoutTwo,  // two columns
            LayoutThree,
            Sigma,
        };

        struct Button
        {
            UINT id = 0;
            Icon icon = Icon::Glyph;
            wchar_t glyph = 0;
            COLORREF tint = CLR_INVALID;  // glyph colour when enabled (default: chrome text)
            int popup = -1;               // IDR_POPUPS submenu shown by the arrow
            bool popupOnly = false;       // the whole button opens the menu
            CString tip;
            CRect rect;
            CRect arrowRect;
            bool enabled = true;
            bool checked = false;
        };

        struct Group
        {
            CString label;
            std::vector<Button> buttons;
            CRect rect;
        };

    protected:
        afx_msg void OnPaint();
        afx_msg BOOL OnEraseBkgnd(CDC* dc);
        afx_msg void OnSize(UINT type, int cx, int cy);
        afx_msg void OnMouseMove(UINT flags, CPoint point);
        afx_msg void OnMouseLeave();
        afx_msg void OnLButtonDown(UINT flags, CPoint point);
        afx_msg void OnLButtonUp(UINT flags, CPoint point);
        DECLARE_MESSAGE_MAP()

    private:
        void BuildGroups();
        void Layout();
        Button* HitTest(CPoint point, bool* onArrow = nullptr);
        void DrawButton(CDC& dc, const Button& button);
        void DrawCustomIcon(CDC& dc, const Button& button, const CRect& rect, COLORREF color);
        void ShowPopup(Button& button);

        std::vector<Group> m_groups;
        CToolTipCtrl m_tips;
        Button* m_hot = nullptr;
        Button* m_pressed = nullptr;
        bool m_pressedArrow = false;
        bool m_tracking = false;
    };
}
