// CommandBar.cpp - the grouped toolbar.
#include "CommandBar.h"

#include "Theme.h"
#include "resource.h"

namespace mf
{
    namespace
    {
        // Segoe MDL2 Assets / Segoe Fluent Icons code points.
        const wchar_t kOpenFile = 0xE8E5;
        const wchar_t kPlay = 0xE768;
        const wchar_t kStop = 0xE71A;
        const wchar_t kUp = 0xE74A;
        const wchar_t kDown = 0xE74B;
        const wchar_t kRefresh = 0xE72C;
        const wchar_t kLightning = 0xE945;
        const wchar_t kSettings = 0xE713;
        const wchar_t kSave = 0xE74E;
        const wchar_t kFont = 0xE8D2;
        const wchar_t kSearch = 0xE721;
        const wchar_t kUndo = 0xE7A7;
        const wchar_t kRedo = 0xE7A6;
        const wchar_t kBack = 0xE72B;
        const wchar_t kForward = 0xE72A;
        const wchar_t kCheck = 0xE73E;
        const wchar_t kOutdent = 0xE290;
        const wchar_t kIndent = 0xE291;
        const wchar_t kChevronDown = 0xE70D;
        const wchar_t kDocument = 0xE8A5;
        const wchar_t kFolder = 0xE8B7;

        CString TipFor(UINT id, const wchar_t* fallback)
        {
            CString text;
            if (text.LoadStringW(id))
            {
                const int newline = text.Find(L'\n');
                if (newline >= 0)
                    return text.Mid(newline + 1);
            }
            return fallback;
        }

        class BarCmdUI : public CCmdUI
        {
        public:
            CommandBar::Button* button = nullptr;
            bool changed = false;

            void Enable(BOOL on) override
            {
                m_bEnableChanged = TRUE;
                if (button->enabled != (on != FALSE))
                {
                    button->enabled = on != FALSE;
                    changed = true;
                }
            }
            void SetCheck(int check) override
            {
                if (button->checked != (check != 0))
                {
                    button->checked = check != 0;
                    changed = true;
                }
            }
            void SetText(LPCTSTR) override
            {
            }
        };
    }

    BEGIN_MESSAGE_MAP(CommandBar, CControlBar)
        ON_WM_PAINT()
        ON_WM_ERASEBKGND()
        ON_WM_SIZE()
        ON_WM_MOUSEMOVE()
        ON_WM_MOUSELEAVE()
        ON_WM_LBUTTONDOWN()
        ON_WM_LBUTTONUP()
    END_MESSAGE_MAP()

    BOOL CommandBar::Create(CWnd* parent)
    {
        m_dwStyle = CBRS_TOP;
        const CString className = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW));
        if (!CWnd::Create(className, L"Toolbar", WS_CHILD | WS_VISIBLE | CBRS_TOP, CRect(), parent, AFX_IDW_TOOLBAR))
            return FALSE;
        SetBarStyle(CBRS_TOP);
        BuildGroups();
        m_tips.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX);
        m_tips.SetMaxTipWidth(400);
        m_tips.Activate(TRUE);
        return TRUE;
    }

    void CommandBar::BuildGroups()
    {
        auto glyph = [](UINT id, wchar_t g, const wchar_t* tip) {
            Button b;
            b.id = id;
            b.glyph = g;
            b.tip = TipFor(id, tip);
            return b;
        };
        auto custom = [](UINT id, Icon icon, const wchar_t* tip) {
            Button b;
            b.id = id;
            b.icon = icon;
            b.tip = TipFor(id, tip);
            return b;
        };
        const COLORREF conflictTint = GetTheme().conflictLine;

        m_groups.clear();
        m_groups.push_back({ L"New Comparison", { custom(ID_FILE_NEW_TWO_WAY, Icon::NewTwo, L"New two-way comparison"),
                                                    custom(ID_FILE_NEW_THREE_WAY, Icon::NewThree, L"New three-way comparison"),
                                                    glyph(ID_FILE_NEW_FOLDERS, kFolder, L"New folder comparison"),
                                                    glyph(ID_FILE_OPEN_IN_PANE, kOpenFile, L"Open file in current pane") } });
        m_groups.push_back({ L"Start/Stop", { glyph(ID_VIEW_START, kPlay, L"Reload and compare"), glyph(ID_VIEW_STOP, kStop, L"Stop") } });
        Button prevConflict = glyph(ID_MERGE_PREV_CONFLICT, kUp, L"Previous conflict");
        Button nextConflict = glyph(ID_MERGE_NEXT_CONFLICT, kDown, L"Next conflict");
        prevConflict.tint = nextConflict.tint = conflictTint;
        m_groups.push_back({ L"Changes", { glyph(ID_MERGE_PREV_CHANGE, kUp, L"Previous change"), glyph(ID_MERGE_NEXT_CHANGE, kDown, L"Next change"),
                                             prevConflict, nextConflict } });
        m_groups.push_back({ L"Recompare", { glyph(ID_VIEW_RECOMPARE, kRefresh, L"Recompare") } });
        m_groups.push_back({ L"Layout", { custom(ID_VIEW_LAYOUT_TWO, Icon::LayoutTwo, L"Two panes"), custom(ID_VIEW_LAYOUT_THREE, Icon::LayoutThree, L"Three panes") } });
        m_groups.push_back({ L"Comparison", { custom(ID_VIEW_STATISTICS, Icon::Sigma, L"Statistics"), glyph(ID_MERGE_AUTO, kLightning, L"Automatic merge") } });
        Button settings = glyph(0, kSettings, L"Comparison settings");
        settings.popup = POPUP_SETTINGS;
        settings.popupOnly = true;
        m_groups.push_back({ L"Settings", { settings } });
        Button encoding = glyph(0, kFont, L"Encoding and line endings of the current pane");
        encoding.popup = POPUP_ENCODING;
        encoding.popupOnly = true;
        m_groups.push_back({ L"Current Pane", { glyph(ID_FILE_SAVE_PANE, kSave, L"Save current pane"), encoding } });
        Button find = glyph(ID_EDIT_FIND_TEXT, kSearch, L"Find");
        find.popup = POPUP_FIND;
        m_groups.push_back({ L"Find", { find } });
        m_groups.push_back({ L"Undo/Redo", { glyph(ID_EDIT_UNDO, kUndo, L"Undo"), glyph(ID_EDIT_REDO, kRedo, L"Redo") } });
        m_groups.push_back({ L"Edits", { glyph(ID_MERGE_COPY_TO_LEFT, kBack, L"Copy change to the left"), glyph(ID_MERGE_COPY_TO_RIGHT, kForward, L"Copy change to the right"),
                                           glyph(ID_MERGE_MARK_RESOLVED, kCheck, L"Mark as resolved") } });
        m_groups.push_back({ L"Indent", { glyph(ID_EDIT_OUTDENT, kOutdent, L"Un-indent"), glyph(ID_EDIT_INDENT, kIndent, L"Indent") } });
    }

    CSize CommandBar::CalcFixedLayout(BOOL, BOOL)
    {
        return CSize(32767, GetFonts().Scale(62));
    }

    void CommandBar::Layout()
    {
        if (!m_hWnd)
            return;
        const Fonts& fonts = GetFonts();
        const int buttonWidth = fonts.Scale(32);
        const int buttonHeight = fonts.Scale(30);
        const int arrowWidth = fonts.Scale(14);
        const int top = fonts.Scale(6);
        const int groupGap = fonts.Scale(10);
        const int padding = fonts.Scale(3);

        CClientDC dc(this);
        CFont* old = dc.SelectObject(const_cast<CFont*>(&fonts.uiSmall));
        int x = fonts.Scale(10);
        int tipId = 1;
        m_tips.DelTool(this, 0);
        for (int id = 1; id < 64; ++id)
            m_tips.DelTool(this, id);
        for (Group& group : m_groups)
        {
            int width = padding;
            for (Button& button : group.buttons)
            {
                const int w = buttonWidth + (button.popup >= 0 ? arrowWidth : 0);
                button.rect = CRect(x + width, top + padding, x + width + w, top + padding + buttonHeight);
                button.arrowRect = button.popup >= 0 ? CRect(button.rect.right - arrowWidth, button.rect.top, button.rect.right, button.rect.bottom) : CRect();
                width += w;
                m_tips.AddTool(this, button.tip, &button.rect, tipId++);
            }
            width += padding;
            const int labelWidth = dc.GetTextExtent(group.label).cx + fonts.Scale(8);
            const int groupWidth = (std::max)(width, labelWidth);
            // Centre the buttons over a wider label.
            const int shift = (groupWidth - width) / 2;
            for (Button& button : group.buttons)
            {
                button.rect.OffsetRect(shift, 0);
                if (!button.arrowRect.IsRectEmpty())
                    button.arrowRect.OffsetRect(shift, 0);
            }
            group.rect = CRect(x, top, x + groupWidth, top + buttonHeight + 2 * padding);
            x += groupWidth + groupGap;
        }
        dc.SelectObject(old);
        // Tool rectangles were taken before centring: refresh them.
        tipId = 1;
        for (Group& group : m_groups)
        {
            for (Button& button : group.buttons)
                m_tips.SetToolRect(this, tipId++, &button.rect);
        }
    }

    void CommandBar::OnSize(UINT type, int cx, int cy)
    {
        CControlBar::OnSize(type, cx, cy);
        Layout();
        Invalidate();
    }

    BOOL CommandBar::OnEraseBkgnd(CDC*)
    {
        return TRUE;
    }

    void CommandBar::OnPaint()
    {
        CPaintDC paint(this);
        CRect client;
        GetClientRect(&client);
        CDC dc;
        dc.CreateCompatibleDC(&paint);
        CBitmap bitmap;
        bitmap.CreateCompatibleBitmap(&paint, client.Width(), client.Height());
        CBitmap* oldBitmap = dc.SelectObject(&bitmap);

        const Theme& theme = GetTheme();
        Fonts& fonts = GetFonts();
        FillSolid(dc, client, theme.chromeBack);
        FillSolid(dc, CRect(client.left, client.bottom - 1, client.right, client.bottom), theme.chromeBorder);

        dc.SetBkMode(TRANSPARENT);
        for (const Group& group : m_groups)
        {
            // A rounded "pill" behind each group's buttons.
            CPen pen(PS_SOLID, 1, theme.chromeBorder);
            CBrush brush(Blend(theme.chromeBack, RGB(255, 255, 255), 55));
            CPen* oldPen = dc.SelectObject(&pen);
            CBrush* oldBrush = dc.SelectObject(&brush);
            CRect pill(group.buttons.front().rect.left - fonts.Scale(3), group.rect.top, group.buttons.back().rect.right + fonts.Scale(3), group.rect.bottom);
            dc.RoundRect(&pill, CPoint(fonts.Scale(10), fonts.Scale(10)));
            dc.SelectObject(oldPen);
            dc.SelectObject(oldBrush);

            for (const Button& button : group.buttons)
                DrawButton(dc, button);

            CRect label(group.rect.left - fonts.Scale(6), group.rect.bottom + fonts.Scale(2), group.rect.right + fonts.Scale(6), client.bottom - fonts.Scale(2));
            CFont* oldFont = dc.SelectObject(&fonts.uiSmall);
            dc.SetTextColor(theme.chromeLabel);
            dc.DrawText(group.label, &label, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            dc.SelectObject(oldFont);
        }

        paint.BitBlt(0, 0, client.Width(), client.Height(), &dc, 0, 0, SRCCOPY);
        dc.SelectObject(oldBitmap);
    }

    void CommandBar::DrawButton(CDC& dc, const Button& button)
    {
        const Theme& theme = GetTheme();
        Fonts& fonts = GetFonts();
        const bool hot = &button == m_hot && button.enabled;
        const bool pressed = &button == m_pressed && hot;
        if (button.checked || hot)
        {
            CBrush brush(pressed ? theme.buttonPressed : (button.checked ? theme.buttonChecked : theme.buttonHover));
            CPen pen(PS_SOLID, 1, pressed ? theme.buttonPressed : (button.checked ? theme.buttonChecked : theme.buttonHover));
            CBrush* oldBrush = dc.SelectObject(&brush);
            CPen* oldPen = dc.SelectObject(&pen);
            CRect r = button.rect;
            r.DeflateRect(1, 1);
            dc.RoundRect(&r, CPoint(fonts.Scale(6), fonts.Scale(6)));
            dc.SelectObject(oldBrush);
            dc.SelectObject(oldPen);
        }
        COLORREF color = button.enabled ? (button.tint != CLR_INVALID ? button.tint : theme.chromeText) : theme.disabled;
        CRect iconRect = button.rect;
        if (!button.arrowRect.IsRectEmpty())
        {
            iconRect.right = button.arrowRect.left;
            DrawGlyph(dc, button.arrowRect, kChevronDown, color, fonts.iconsSmall);
        }
        if (button.icon == Icon::Glyph)
            DrawGlyph(dc, iconRect, button.glyph, color, fonts.icons);
        else
            DrawCustomIcon(dc, button, iconRect, color);
    }

    void CommandBar::DrawCustomIcon(CDC& dc, const Button& button, const CRect& rect, COLORREF color)
    {
        Fonts& fonts = GetFonts();
        const CPoint c = rect.CenterPoint();
        CPen pen(PS_SOLID, (std::max)(1, fonts.Scale(1)), color);
        CPen* oldPen = dc.SelectObject(&pen);
        CBrush* oldBrush = static_cast<CBrush*>(dc.SelectStockObject(NULL_BRUSH));
        switch (button.icon)
        {
        case Icon::LayoutTwo:
        case Icon::LayoutThree:
        {
            const int w = fonts.Scale(18), h = fonts.Scale(14);
            CRect frame(c.x - w / 2, c.y - h / 2, c.x + w / 2, c.y + h / 2);
            dc.RoundRect(&frame, CPoint(fonts.Scale(3), fonts.Scale(3)));
            const int columns = button.icon == Icon::LayoutTwo ? 2 : 3;
            for (int k = 1; k < columns; ++k)
            {
                const int x = frame.left + frame.Width() * k / columns;
                dc.MoveTo(x, frame.top);
                dc.LineTo(x, frame.bottom);
            }
            break;
        }
        case Icon::NewTwo:
        case Icon::NewThree:
        {
            CRect glyphRect = rect;
            glyphRect.OffsetRect(-fonts.Scale(3), 0);
            DrawGlyph(dc, glyphRect, kDocument, color, fonts.icons);
            CRect badge(c.x + fonts.Scale(1), c.y, c.x + fonts.Scale(11), c.y + fonts.Scale(11));
            CBrush fill(button.enabled ? GetTheme().accent : GetTheme().disabled);
            CPen none(PS_NULL, 0, RGB(0, 0, 0));
            dc.SelectObject(&fill);
            dc.SelectObject(&none);
            dc.Ellipse(&badge);
            CFont* oldFont = dc.SelectObject(&fonts.uiSmall);
            dc.SetTextColor(RGB(255, 255, 255));
            dc.DrawText(button.icon == Icon::NewTwo ? L"2" : L"3", &badge, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            dc.SelectObject(oldFont);
            dc.SelectObject(&pen);
            break;
        }
        case Icon::Sigma:
        {
            CFont* oldFont = dc.SelectObject(&fonts.uiBold);
            dc.SetTextColor(color);
            CRect r = rect;
            LOGFONTW lf = {};
            fonts.uiBold.GetLogFont(&lf);
            lf.lfHeight = -fonts.Scale(19);
            CFont big;
            big.CreateFontIndirectW(&lf);
            dc.SelectObject(&big);
            dc.DrawText(L"\x03A3", 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            dc.SelectObject(oldFont);
            break;
        }
        default:
            break;
        }
        dc.SelectObject(oldPen);
        dc.SelectObject(oldBrush);
    }

    CommandBar::Button* CommandBar::HitTest(CPoint point, bool* onArrow)
    {
        for (Group& group : m_groups)
        {
            for (Button& button : group.buttons)
            {
                if (button.rect.PtInRect(point))
                {
                    if (onArrow)
                        *onArrow = button.popupOnly || (!button.arrowRect.IsRectEmpty() && button.arrowRect.PtInRect(point));
                    return &button;
                }
            }
        }
        return nullptr;
    }

    void CommandBar::OnMouseMove(UINT flags, CPoint point)
    {
        Button* hot = HitTest(point);
        if (hot != m_hot)
        {
            m_hot = hot;
            Invalidate(FALSE);
        }
        if (!m_tracking)
        {
            TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, m_hWnd, 0 };
            m_tracking = TrackMouseEvent(&track) != FALSE;
        }
        CControlBar::OnMouseMove(flags, point);
    }

    void CommandBar::OnMouseLeave()
    {
        m_tracking = false;
        if (m_hot)
        {
            m_hot = nullptr;
            Invalidate(FALSE);
        }
    }

    void CommandBar::OnLButtonDown(UINT, CPoint point)
    {
        bool onArrow = false;
        Button* button = HitTest(point, &onArrow);
        if (!button || !button->enabled)
            return;
        if (onArrow && button->popup >= 0)
        {
            ShowPopup(*button);
            return;
        }
        m_pressed = button;
        SetCapture();
        Invalidate(FALSE);
    }

    void CommandBar::OnLButtonUp(UINT, CPoint point)
    {
        if (!m_pressed)
            return;
        Button* pressed = m_pressed;
        m_pressed = nullptr;
        ReleaseCapture();
        Invalidate(FALSE);
        if (HitTest(point) == pressed && pressed->enabled && pressed->id)
            GetParentFrame()->SendMessage(WM_COMMAND, pressed->id);
    }

    void CommandBar::ShowPopup(Button& button)
    {
        CMenu menu;
        if (!menu.LoadMenuW(IDR_POPUPS))
            return;
        CMenu* popup = menu.GetSubMenu(button.popup);
        if (!popup)
            return;
        CRect rect = button.rect;
        ClientToScreen(&rect);
        m_pressed = &button;
        Invalidate(FALSE);
        UpdateWindow();
        // Owned by the frame: it updates check marks (WM_INITMENUPOPUP) and receives the command.
        popup->TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON, rect.left, rect.bottom, GetParentFrame());
        m_pressed = nullptr;
        Invalidate(FALSE);
    }

    void CommandBar::OnUpdateCmdUI(CFrameWnd* target, BOOL disableIfNoHandler)
    {
        bool changed = false;
        BarCmdUI ui;
        for (Group& group : m_groups)
        {
            for (Button& button : group.buttons)
            {
                if (!button.id)
                    continue;
                ui.button = &button;
                ui.changed = false;
                ui.m_nID = button.id;
                ui.m_pOther = this;
                ui.DoUpdate(target, disableIfNoHandler);
                changed = changed || ui.changed;
            }
        }
        if (changed)
            Invalidate(FALSE);
    }

    BOOL CommandBar::PreTranslateMessage(MSG* msg)
    {
        if (m_tips.m_hWnd)
            m_tips.RelayEvent(msg);
        return CControlBar::PreTranslateMessage(msg);
    }
}
