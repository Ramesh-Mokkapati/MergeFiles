// LinkGutter.cpp - linking lines between corresponding blocks of two panes.
#include "LinkGutter.h"

#include "CompareWnd.h"
#include "PaneView.h"
#include "Theme.h"

namespace mf
{
    BEGIN_MESSAGE_MAP(LinkGutter, CWnd)
        ON_WM_PAINT()
        ON_WM_ERASEBKGND()
        ON_WM_MOUSEWHEEL()
    END_MESSAGE_MAP()

    BOOL LinkGutter::Create(CWnd* parent)
    {
        const CString className = AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW));
        return CWnd::Create(className, L"Links", WS_CHILD | WS_VISIBLE, CRect(), parent, 200 + m_left);
    }

    BOOL LinkGutter::OnEraseBkgnd(CDC*)
    {
        return TRUE;
    }

    BOOL LinkGutter::OnMouseWheel(UINT flags, short delta, CPoint point)
    {
        // Scrolling over the gutter scrolls the active pane (and so all panes).
        PaneView* pane = m_owner.Pane(m_owner.ActivePane());
        return pane ? static_cast<BOOL>(pane->SendMessage(WM_MOUSEWHEEL, MAKEWPARAM(flags, delta), MAKELPARAM(point.x, point.y))) : FALSE;
    }

    void LinkGutter::OnPaint()
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
        dc.FillSolidRect(&client, theme.gutterBack);
        dc.FillSolidRect(0, 0, 1, client.Height(), theme.chromeBorder);
        dc.FillSolidRect(client.right - 1, 0, 1, client.Height(), theme.chromeBorder);

        const int right = m_left + 1;
        PaneView* leftPane = m_owner.Pane(m_left);
        PaneView* rightPane = m_owner.Pane(right);
        const Comparison& doc = m_owner.Doc();
        if (leftPane && rightPane)
        {
            const int w = client.Width();
            const int current = m_owner.CurrentChunk();
            const std::vector<Chunk>& chunks = doc.Chunks();
            for (size_t k = 0; k < chunks.size(); ++k)
            {
                const Chunk& chunk = chunks[k];
                if (!chunk.IsDifference() || !doc.PaneDiffersInChunk(chunk, m_left) || !doc.PaneDiffersInChunk(chunk, right))
                    continue;
                const int a0 = leftPane->ChunkTopY(static_cast<int>(k));
                const int a1 = leftPane->ChunkBottomY(static_cast<int>(k));
                const int b0 = rightPane->ChunkTopY(static_cast<int>(k));
                const int b1 = rightPane->ChunkBottomY(static_cast<int>(k));
                if ((std::max)(a1, b1) < -2 || (std::min)(a0, b0) > client.Height() + 2)
                    continue;
                const ChunkStyle style = doc.StyleInPane(chunk, doc.ReferencePane() == m_left ? right : m_left);
                const bool isCurrent = static_cast<int>(k) == current;

                // The block's outline: top curve left to right, bottom curve right to left.
                const int c1 = w / 3, c2 = 2 * w / 3;
                POINT top[4] = { { 0, a0 }, { c1, a0 }, { c2, b0 }, { w, b0 } };
                POINT bottom[4] = { { w, b1 }, { c2, b1 }, { c1, a1 }, { 0, a1 } };
                CBrush brush(Blend(theme.StyleBack(style), theme.StyleLine(style), isCurrent ? 30 : 12));
                CPen pen(PS_SOLID, isCurrent ? GetFonts().Scale(2) : 1, isCurrent ? Blend(theme.StyleLine(style), RGB(0, 0, 0), 25) : theme.StyleLine(style));
                CBrush* ob = dc.SelectObject(&brush);
                CPen* op = dc.SelectObject(&pen);
                dc.BeginPath();
                dc.MoveTo(top[0]);
                dc.PolyBezierTo(top + 1, 3);
                dc.LineTo(bottom[0]);
                dc.PolyBezierTo(bottom + 1, 3);
                dc.CloseFigure();
                dc.EndPath();
                dc.StrokeAndFillPath();
                dc.SelectObject(ob);
                dc.SelectObject(op);
            }

            // Synchronization links: a bold dashed line joining the linked lines.
            CPen syncPen(PS_SOLID, GetFonts().Scale(2), theme.syncLink);
            CPen* oldPen = dc.SelectObject(&syncPen);
            for (const SyncPoint& point : doc.SyncPoints())
            {
                const int a = leftPane->LineToY(point.line[m_left]);
                const int b = rightPane->LineToY(point.line[right]);
                if ((std::max)(a, b) < 0 || (std::min)(a, b) > client.Height())
                    continue;
                dc.MoveTo(0, a);
                dc.LineTo(w, b);
                const int r = GetFonts().Scale(3);
                dc.FillSolidRect(0, a - r, r * 2, r * 2, theme.syncLink);
                dc.FillSolidRect(w - r * 2, b - r, r * 2, r * 2, theme.syncLink);
            }
            dc.SelectObject(oldPen);
        }

        paint.BitBlt(0, 0, client.Width(), client.Height(), &dc, 0, 0, SRCCOPY);
        dc.SelectObject(oldBitmap);
    }
}
