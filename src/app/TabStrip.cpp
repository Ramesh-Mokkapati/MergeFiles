// TabStrip.cpp - custom-drawn comparison tabs.
#include "TabStrip.h"

#include "Theme.h"

namespace mf
{
    namespace
    {
        const wchar_t kDocumentGlyph = 0xE8A5;
        const wchar_t kFolderGlyph = 0xE8B7;
        const wchar_t kImageGlyph = 0xEB9F;
        const wchar_t kCloseGlyph = 0xE711;
        const wchar_t kBinaryGlyph = 0xE943;  // Code
        const wchar_t kPatchGlyph = 0xE90F;   // Repair
    }

    BEGIN_MESSAGE_MAP(TabStrip, CControlBar)
        ON_WM_PAINT()
        ON_WM_ERASEBKGND()
        ON_WM_SIZE()
        ON_WM_MOUSEMOVE()
        ON_WM_MOUSELEAVE()
        ON_WM_LBUTTONDOWN()
        ON_WM_LBUTTONUP()
        ON_WM_MBUTTONUP()
    END_MESSAGE_MAP()

    BOOL TabStrip::Create(CWnd* parent, TabStripOwner* owner)
    {
        m_owner = owner;
        m_dwStyle = CBRS_TOP;
        const CString className = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW));
        if (!CWnd::Create(className, L"Tabs", WS_CHILD | WS_VISIBLE | CBRS_TOP, CRect(), parent, AFX_IDW_CONTROLBAR_FIRST + 30))
            return FALSE;
        SetBarStyle(CBRS_TOP);
        m_tips.Create(this, TTS_ALWAYSTIP | TTS_NOPREFIX);
        m_tips.SetMaxTipWidth(800);
        return TRUE;
    }

    CSize TabStrip::CalcFixedLayout(BOOL, BOOL)
    {
        return CSize(32767, GetFonts().Scale(30));
    }

    void TabStrip::SetTabs(const std::vector<TabItem>& tabs, int active)
    {
        m_tabs = tabs;
        m_active = active;
        m_hot = -1;
        Layout();
        Invalidate(FALSE);
    }

    void TabStrip::Layout()
    {
        if (!m_hWnd)
            return;
        const Fonts& fonts = GetFonts();
        CRect client;
        GetClientRect(&client);
        m_rects.clear();
        m_closeRects.clear();
        for (int id = 1; id <= 256; ++id)
            m_tips.DelTool(this, id);
        if (m_tabs.empty())
            return;
        CClientDC dc(this);
        CFont* old = dc.SelectObject(const_cast<CFont*>(&fonts.uiBold));
        const int margin = fonts.Scale(6);
        const int minWidth = fonts.Scale(90);
        const int maxWidth = fonts.Scale(320);
        const int available = client.Width() - 2 * margin;
        // Tabs share the width equally when there are too many to show at their natural width.
        int natural = 0;
        std::vector<int> widths;
        for (const TabItem& tab : m_tabs)
        {
            const int w = (std::min)(maxWidth, (std::max)(minWidth, static_cast<int>(dc.GetTextExtent(tab.title + L" *").cx) + fonts.Scale(60)));
            widths.push_back(w);
            natural += w;
        }
        dc.SelectObject(old);
        const int count = static_cast<int>(m_tabs.size());
        const bool squeeze = natural > available;
        int x = margin;
        for (int i = 0; i < count; ++i)
        {
            const int w = squeeze ? (std::max)(fonts.Scale(40), available / count) : widths[static_cast<size_t>(i)];
            CRect rect(x, fonts.Scale(3), x + w, client.bottom);
            m_rects.push_back(rect);
            const int size = fonts.Scale(16);
            m_closeRects.push_back(CRect(rect.right - size - fonts.Scale(6), rect.CenterPoint().y - size / 2 + 1, rect.right - fonts.Scale(6), rect.CenterPoint().y + size / 2 + 1));
            m_tips.AddTool(this, m_tabs[static_cast<size_t>(i)].tooltip, &rect, static_cast<UINT_PTR>(i + 1));
            x += w;
        }
    }

    void TabStrip::OnSize(UINT type, int cx, int cy)
    {
        CControlBar::OnSize(type, cx, cy);
        Layout();
        Invalidate(FALSE);
    }

    BOOL TabStrip::OnEraseBkgnd(CDC*)
    {
        return TRUE;
    }

    void TabStrip::OnPaint()
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
        FillSolid(dc, client, theme.tabInactive);
        FillSolid(dc, CRect(client.left, client.bottom - 1, client.right, client.bottom), theme.chromeBorder);
        dc.SetBkMode(TRANSPARENT);

        for (size_t i = 0; i < m_tabs.size(); ++i)
        {
            const TabItem& tab = m_tabs[i];
            CRect rect = m_rects[i];
            const bool active = static_cast<int>(i) == m_active;
            const bool hot = static_cast<int>(i) == m_hot;
            if (active)
            {
                FillSolid(dc, CRect(rect.left, rect.top, rect.right, rect.bottom), theme.tabActive);
                FillSolid(dc, CRect(rect.left, rect.top, rect.right, rect.top + fonts.Scale(2)), theme.accent);
                FillSolid(dc, CRect(rect.left, rect.top, rect.left + 1, rect.bottom), theme.chromeBorder);
                FillSolid(dc, CRect(rect.right - 1, rect.top, rect.right, rect.bottom), theme.chromeBorder);
            }
            else
            {
                if (hot)
                    FillSolid(dc, CRect(rect.left, rect.top, rect.right, rect.bottom - 1), theme.buttonHover);
                if (static_cast<int>(i) + 1 != m_active)
                    FillSolid(dc, CRect(rect.right - 1, rect.top + fonts.Scale(5), rect.right, rect.bottom - fonts.Scale(5)), theme.chromeBorder);
            }

            const wchar_t glyph = tab.kind == TabKind::Folder ? kFolderGlyph
                : tab.kind == TabKind::Image                ? kImageGlyph
                : tab.kind == TabKind::Binary               ? kBinaryGlyph
                : tab.kind == TabKind::Patch                ? kPatchGlyph
                                                            : kDocumentGlyph;
            CRect iconRect(rect.left + fonts.Scale(6), rect.top, rect.left + fonts.Scale(26), rect.bottom);
            DrawGlyph(dc, iconRect, glyph, active ? theme.accent : theme.chromeLabel, fonts.icons);

            CRect textRect(iconRect.right + fonts.Scale(4), rect.top, m_closeRects[i].left - fonts.Scale(4), rect.bottom);
            CFont* oldFont = dc.SelectObject(active ? &fonts.uiBold : &fonts.ui);
            dc.SetTextColor(active ? theme.text : theme.chromeText);
            const CString title = tab.modified ? tab.title + L" \x25CF" : tab.title;
            dc.DrawText(title, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            dc.SelectObject(oldFont);

            if (active || hot)
            {
                const CRect& close = m_closeRects[i];
                if (hot && m_hotClose)
                {
                    CBrush brush(theme.buttonPressed);
                    CPen pen(PS_SOLID, 1, theme.buttonPressed);
                    CBrush* ob = dc.SelectObject(&brush);
                    CPen* op = dc.SelectObject(&pen);
                    dc.RoundRect(&close, CPoint(fonts.Scale(4), fonts.Scale(4)));
                    dc.SelectObject(ob);
                    dc.SelectObject(op);
                }
                DrawGlyph(dc, close, kCloseGlyph, theme.chromeText, fonts.iconsSmall);
            }
        }

        if (m_tabs.empty())
        {
            CFont* oldFont = dc.SelectObject(&fonts.ui);
            dc.SetTextColor(theme.chromeLabel);
            CRect r = client;
            r.left += fonts.Scale(12);
            dc.DrawText(L"No comparisons open", &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            dc.SelectObject(oldFont);
        }

        paint.BitBlt(0, 0, client.Width(), client.Height(), &dc, 0, 0, SRCCOPY);
        dc.SelectObject(oldBitmap);
    }

    int TabStrip::HitTest(CPoint point, bool* onClose) const
    {
        for (size_t i = 0; i < m_rects.size(); ++i)
        {
            if (m_rects[i].PtInRect(point))
            {
                if (onClose)
                    *onClose = m_closeRects[i].PtInRect(point) != FALSE;
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void TabStrip::OnMouseMove(UINT flags, CPoint point)
    {
        bool onClose = false;
        const int hot = HitTest(point, &onClose);
        if (hot != m_hot || onClose != m_hotClose)
        {
            m_hot = hot;
            m_hotClose = onClose;
            Invalidate(FALSE);
        }
        if (!m_tracking)
        {
            TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, m_hWnd, 0 };
            m_tracking = TrackMouseEvent(&track) != FALSE;
        }
        CControlBar::OnMouseMove(flags, point);
    }

    void TabStrip::OnMouseLeave()
    {
        m_tracking = false;
        m_hot = -1;
        m_hotClose = false;
        Invalidate(FALSE);
    }

    void TabStrip::OnLButtonDown(UINT, CPoint point)
    {
        bool onClose = false;
        const int index = HitTest(point, &onClose);
        if (index >= 0 && !onClose && m_owner)
            m_owner->OnTabActivate(index);
    }

    void TabStrip::OnLButtonUp(UINT, CPoint point)
    {
        bool onClose = false;
        const int index = HitTest(point, &onClose);
        if (index >= 0 && onClose && m_owner)
            m_owner->OnTabClose(index);
    }

    void TabStrip::OnMButtonUp(UINT, CPoint point)
    {
        const int index = HitTest(point);
        if (index >= 0 && m_owner)
            m_owner->OnTabClose(index);
    }

    BOOL TabStrip::PreTranslateMessage(MSG* msg)
    {
        if (m_tips.m_hWnd)
            m_tips.RelayEvent(msg);
        return CControlBar::PreTranslateMessage(msg);
    }
}
