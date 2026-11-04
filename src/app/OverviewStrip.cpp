// OverviewStrip.cpp - whole-file map of the changes in one pane.
#include "OverviewStrip.h"

#include "CompareWnd.h"
#include "PaneView.h"
#include "Theme.h"

namespace mf
{
    BEGIN_MESSAGE_MAP(OverviewStrip, CWnd)
        ON_WM_PAINT()
        ON_WM_ERASEBKGND()
        ON_WM_LBUTTONDOWN()
        ON_WM_MOUSEMOVE()
        ON_WM_LBUTTONUP()
    END_MESSAGE_MAP()

    BOOL OverviewStrip::Create(CWnd* parent)
    {
        const CString className = AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_HAND));
        return CWnd::Create(className, L"Overview", WS_CHILD | WS_VISIBLE, CRect(), parent, 300 + m_pane);
    }

    BOOL OverviewStrip::OnEraseBkgnd(CDC*)
    {
        return TRUE;
    }

    void OverviewStrip::OnPaint()
    {
        CPaintDC paint(this);
        CRect client;
        GetClientRect(&client);
        if (client.IsRectEmpty())
            return;
        CDC dc;
        dc.CreateCompatibleDC(&paint);
        CBitmap bitmap;
        bitmap.CreateCompatibleBitmap(&paint, client.Width(), client.Height());
        CBitmap* oldBitmap = dc.SelectObject(&bitmap);

        const Theme& theme = GetTheme();
        const Comparison& doc = m_owner.Doc();
        dc.FillSolidRect(&client, theme.gutterBack);
        const int lines = (std::max)(1, doc.Buffer(m_pane).LineCount());
        const double scale = static_cast<double>(client.Height()) / lines;
        const int inset = GetFonts().Scale(2);
        for (const Chunk& chunk : doc.Chunks())
        {
            const ChunkStyle style = doc.StyleInPane(chunk, m_pane);
            if (style == ChunkStyle::None)
                continue;
            const int y0 = static_cast<int>(chunk.start[m_pane] * scale);
            const int y1 = (std::max)(y0 + GetFonts().Scale(2), static_cast<int>(chunk.End(m_pane) * scale));
            dc.FillSolidRect(inset, y0, client.Width() - 2 * inset, y1 - y0, chunk.count[m_pane] == 0 ? theme.StyleLine(style) : theme.StyleChar(style));
        }
        // Bookmarks: short ticks across the strip.
        const TextBuffer& buffer = doc.Buffer(m_pane);
        for (int line = 0; line < buffer.LineCount(); ++line)
        {
            if (buffer.GetLine(line).flags & LineBookmark)
                dc.FillSolidRect(0, static_cast<int>(line * scale), client.Width(), (std::max)(2, GetFonts().Scale(2)), theme.bookmark);
        }
        if (PaneView* pane = m_owner.Pane(m_pane))
        {
            const int y0 = static_cast<int>(pane->TopLine() * scale);
            const int y1 = (std::max)(y0 + GetFonts().Scale(4), static_cast<int>((pane->BottomLine() + 1) * scale));
            CRect view(0, y0, client.Width(), (std::min)(y1, static_cast<int>(client.bottom)));
            CBrush frame(theme.linkLine);
            dc.FrameRect(&view, &frame);
        }

        paint.BitBlt(0, 0, client.Width(), client.Height(), &dc, 0, 0, SRCCOPY);
        dc.SelectObject(oldBitmap);
    }

    void OverviewStrip::JumpTo(CPoint point)
    {
        CRect client;
        GetClientRect(&client);
        const int lines = m_owner.Doc().Buffer(m_pane).LineCount();
        const int line = static_cast<int>(static_cast<double>((std::max)(0L, (std::min)(point.y, client.bottom))) * lines / (std::max)(1, client.Height()));
        m_owner.JumpToLine(m_pane, line);
    }

    void OverviewStrip::OnLButtonDown(UINT, CPoint point)
    {
        m_dragging = true;
        SetCapture();
        JumpTo(point);
    }

    void OverviewStrip::OnMouseMove(UINT flags, CPoint point)
    {
        if (m_dragging && (flags & MK_LBUTTON))
            JumpTo(point);
    }

    void OverviewStrip::OnLButtonUp(UINT, CPoint)
    {
        if (m_dragging)
        {
            m_dragging = false;
            ReleaseCapture();
        }
    }
}
