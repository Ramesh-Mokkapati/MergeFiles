// LineDetailPanel.cpp - the current lines of all files, one above the other.
#include "LineDetailPanel.h"

#include "CompareWnd.h"
#include "PaneView.h"
#include "Theme.h"

namespace mf
{
    namespace
    {
        const int kRowsPerPane = 2;
    }

    BEGIN_MESSAGE_MAP(LineDetailPanel, CWnd)
        ON_WM_PAINT()
        ON_WM_ERASEBKGND()
    END_MESSAGE_MAP()

    BOOL LineDetailPanel::Create(CWnd* parent)
    {
        const CString className = AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW));
        return CWnd::Create(className, L"Line detail", WS_CHILD, CRect(), parent, 400);
    }

    int LineDetailPanel::RowHeight() const
    {
        CClientDC dc(const_cast<LineDetailPanel*>(this));
        CFont* old = dc.SelectObject(&GetFonts().code);
        TEXTMETRICW tm = {};
        dc.GetTextMetrics(&tm);
        dc.SelectObject(old);
        return tm.tmHeight + GetFonts().Scale(3);
    }

    int LineDetailPanel::PreferredHeight(int panes) const
    {
        if (!m_hWnd)
            return 0;
        return GetFonts().Scale(10) + panes * kRowsPerPane * RowHeight();
    }

    BOOL LineDetailPanel::OnEraseBkgnd(CDC*)
    {
        return TRUE;
    }

    void LineDetailPanel::OnPaint()
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
        Fonts& fonts = GetFonts();
        const Comparison& doc = m_owner.Doc();
        dc.FillSolidRect(&client, theme.pathBarBack);
        dc.FillSolidRect(0, 0, client.Width(), 1, theme.chromeBorder);
        dc.SetBkMode(TRANSPARENT);

        const int panes = m_owner.PaneCount();
        const int active = m_owner.ActivePane();
        PaneView* activeView = m_owner.Pane(active);
        if (!activeView)
            return;
        const int caretLine = activeView->Caret().line;
        int lines[3] = { -1, -1, -1 };
        for (int pane = 0; pane < panes; ++pane)
            lines[pane] = m_owner.CorrespondingLine(active, caretLine, pane);

        static const wchar_t* twoWay[] = { L"Left", L"Right" };
        static const wchar_t* threeWay[] = { L"Left", L"Ancestor", L"Right" };
        const int rowHeight = RowHeight();
        const int labelWidth = fonts.Scale(96);
        const int textLeft = labelWidth + fonts.Scale(8);
        CFont* oldFont = dc.SelectObject(&fonts.code);
        const int charWidth = (std::max)(1, static_cast<int>(dc.GetTextExtent(L"0", 1).cx));
        const int wrap = (std::max)(10, (client.Width() - textLeft - fonts.Scale(8)) / charWidth);
        const int tabSize = m_owner.GetViewSettings().tabSize;

        for (int pane = 0; pane < panes; ++pane)
        {
            const int y = fonts.Scale(5) + pane * kRowsPerPane * rowHeight;
            if (pane == active)
                dc.FillSolidRect(0, y - 1, client.Width(), kRowsPerPane * rowHeight + 2, Blend(theme.pathBarBack, theme.accent, 8));

            CString label;
            label.Format(lines[pane] >= 0 ? L"%s %d" : L"%s", doc.IsThreeWay() ? threeWay[pane] : twoWay[pane], lines[pane] + 1);
            dc.SelectObject(&fonts.uiSmall);
            dc.SetTextColor(pane == active ? theme.accent : theme.chromeLabel);
            CRect labelRect(fonts.Scale(8), y, labelWidth, y + rowHeight);
            dc.DrawText(label, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            dc.SelectObject(&fonts.code);

            if (lines[pane] < 0)
            {
                dc.SetTextColor(theme.chromeLabel);
                CRect none(textLeft, y, client.right, y + rowHeight);
                dc.DrawText(L"(no corresponding line)", &none, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                continue;
            }

            // Highlight against the active line (the active line against the first other line).
            const std::wstring& text = doc.Buffer(pane).LineText(lines[pane]);
            int other = active;
            if (pane == active)
            {
                other = -1;
                for (int candidate = 0; candidate < panes && other < 0; ++candidate)
                {
                    if (candidate != active && lines[candidate] >= 0)
                        other = candidate;
                }
            }
            std::vector<CharRange> ranges;
            if (other >= 0)
                ranges = DiffLineChars(text, doc.Buffer(other).LineText(lines[other]), doc.Options()).a;

            std::vector<int> cols;
            BuildColumns(text, tabSize, cols);
            std::wstring display;
            std::vector<int> dx, displayStart(text.size() + 1);
            for (size_t i = 0; i < text.size(); ++i)
            {
                displayStart[i] = static_cast<int>(display.size());
                const int columns = cols[i + 1] - cols[i];
                if (text[i] == L'\t')
                {
                    for (int k = 0; k < columns; ++k)
                    {
                        display.push_back(L' ');
                        dx.push_back(charWidth);
                    }
                }
                else
                {
                    display.push_back(text[i] < 0x20 ? static_cast<wchar_t>(0x2400 + text[i]) : text[i]);
                    dx.push_back(columns * charWidth);
                }
            }
            displayStart[text.size()] = static_cast<int>(display.size());

            for (int segment = 0; segment < kRowsPerPane; ++segment)
            {
                const int rowY = y + segment * rowHeight;
                const int segStart = segment * wrap;
                const int segEnd = segment + 1 < kRowsPerPane ? (segment + 1) * wrap : INT_MAX / 2;
                const int left = textLeft - segStart * charWidth;
                for (const CharRange& range : ranges)
                {
                    const int c0 = (std::max)(segStart, cols[static_cast<size_t>(range.start)]);
                    const int c1 = (std::min)(segEnd, cols[static_cast<size_t>((std::min)(static_cast<int>(text.size()), range.start + range.length))]);
                    if (c1 > c0)
                        dc.FillSolidRect(left + c0 * charWidth, rowY + 1, (c1 - c0) * charWidth, rowHeight - 2, theme.changeChar);
                }
                // The characters of this segment.
                size_t first = 0;
                while (first < text.size() && cols[first + 1] <= segStart)
                    ++first;
                size_t last = first;
                while (last < text.size() && cols[last] < segEnd)
                    ++last;
                if (last > first)
                {
                    CRect clip(textLeft, rowY, client.right - fonts.Scale(8), rowY + rowHeight);
                    dc.SetTextColor(theme.text);
                    const int start = displayStart[first];
                    ::ExtTextOutW(dc.m_hDC, left + cols[first] * charWidth, rowY + fonts.Scale(1), ETO_CLIPPED, &clip, display.c_str() + start,
                        static_cast<UINT>(displayStart[last] - start), dx.data() + start);
                }
            }
        }

        dc.SelectObject(oldFont);
        paint.BitBlt(0, 0, client.Width(), client.Height(), &dc, 0, 0, SRCCOPY);
        dc.SelectObject(oldBitmap);
    }
}
