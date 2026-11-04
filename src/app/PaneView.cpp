// PaneView.cpp - drawing and editing one file of a comparison.
#include "PaneView.h"

#include "CompareWnd.h"
#include "Theme.h"
#include "resource.h"

namespace mf
{
    namespace
    {
        const UINT_PTR kDragTimer = 1;

        bool IsLowSurrogate(wchar_t c)
        {
            return c >= 0xDC00 && c <= 0xDFFF;
        }

        bool IsWordChar(wchar_t c)
        {
            return c == L'_' || iswalnum(c);
        }

        int Digits(int value)
        {
            int digits = 1;
            while (value >= 10)
            {
                value /= 10;
                ++digits;
            }
            return digits;
        }
    }

    BEGIN_MESSAGE_MAP(PaneView, CWnd)
        ON_WM_PAINT()
        ON_WM_ERASEBKGND()
        ON_WM_SIZE()
        ON_WM_VSCROLL()
        ON_WM_HSCROLL()
        ON_WM_MOUSEWHEEL()
        ON_WM_MOUSEHWHEEL()
        ON_WM_LBUTTONDOWN()
        ON_WM_LBUTTONDBLCLK()
        ON_WM_LBUTTONUP()
        ON_WM_MOUSEMOVE()
        ON_WM_RBUTTONDOWN()
        ON_WM_CONTEXTMENU()
        ON_WM_KEYDOWN()
        ON_WM_CHAR()
        ON_WM_SETFOCUS()
        ON_WM_KILLFOCUS()
        ON_WM_SETCURSOR()
        ON_WM_TIMER()
        ON_WM_CAPTURECHANGED()
        ON_WM_GETDLGCODE()
        ON_MESSAGE(WM_IME_STARTCOMPOSITION, &PaneView::OnImeStartComposition)
    END_MESSAGE_MAP()

    PaneView::PaneView(CompareWnd& owner, int pane) : m_owner(owner), m_pane(pane)
    {
    }

    BOOL PaneView::Create(CWnd* parent)
    {
        const CString className = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_IBEAM));
        if (!CWnd::Create(className, L"Pane", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, CRect(), parent, 100 + m_pane))
            return FALSE;
        UpdateMetrics();
        Refresh();
        return TRUE;
    }

    void PaneView::SetPane(int pane)
    {
        m_pane = pane;
        m_states.clear();
        m_statesRevision = 0;
        m_top = 0;
        m_xOffset = 0;
        m_caret = m_anchor = TextPos();
        Refresh();
    }

    const TextBuffer& PaneView::Buffer() const
    {
        return m_owner.Doc().Buffer(m_pane);
    }

    // ---- geometry ----

    void PaneView::UpdateMetrics()
    {
        if (!m_hWnd)
            return;
        CClientDC dc(this);
        CFont* old = dc.SelectObject(&GetFonts().code);
        TEXTMETRICW tm = {};
        dc.GetTextMetrics(&tm);
        m_charWidth = (std::max)(1, static_cast<int>(dc.GetTextExtent(L"0", 1).cx));
        m_lineHeight = tm.tmHeight + GetFonts().Scale(4);
        dc.SelectObject(old);
        if (m_hasFocus)
        {
            DestroyCaret();
            CreateSolidCaret(GetFonts().Scale(2), m_lineHeight);
            ShowCaret();
        }
        m_layoutRevision = 0;
        Refresh();
    }

    int PaneView::VisibleRows() const
    {
        CRect client;
        GetClientRect(&client);
        return (std::max)(1, client.Height() / m_lineHeight);
    }

    int PaneView::TextLeft() const
    {
        return m_marginWidth + GetFonts().Scale(6);
    }

    int PaneView::TextAreaColumns() const
    {
        CRect client;
        GetClientRect(&client);
        return (std::max)(20, (client.Width() - TextLeft() - GetFonts().Scale(28)) / m_charWidth);
    }

    int PaneView::WrapColumns() const
    {
        return m_owner.Rows().wrapColumns;
    }

    int PaneView::OwnRows(int line) const
    {
        return m_owner.Rows().ownRows[static_cast<size_t>(m_pane)][static_cast<size_t>(line)];
    }

    int PaneView::LineRow(int line) const
    {
        const std::vector<int>& rows = m_owner.Rows().lineRow[static_cast<size_t>(m_pane)];
        line = (std::max)(0, (std::min)(line, static_cast<int>(rows.size()) - 1));
        return rows[static_cast<size_t>(line)];
    }

    int PaneView::TotalRows() const
    {
        return m_owner.Rows().lineRow[static_cast<size_t>(m_pane)].back();
    }

    int PaneView::MaxTopRow() const
    {
        return (std::max)(0, TotalRows() - (std::max)(1, VisibleRows() / 2));
    }

    int PaneView::LineToY(int line) const
    {
        return (LineRow(line) - m_top) * m_lineHeight;
    }

    int PaneView::ChunkTopY(int chunk) const
    {
        return (m_owner.Rows().chunkRow[static_cast<size_t>(m_pane)][static_cast<size_t>(chunk)] - m_top) * m_lineHeight;
    }

    int PaneView::ChunkBottomY(int chunk) const
    {
        return (m_owner.Rows().chunkRow[static_cast<size_t>(m_pane)][static_cast<size_t>(chunk) + 1] - m_top) * m_lineHeight;
    }

    bool PaneView::RowToLine(int row, int& line, int& segment) const
    {
        const std::vector<int>& rows = m_owner.Rows().lineRow[static_cast<size_t>(m_pane)];
        const int lines = Buffer().LineCount();
        // Last line whose first row is at or before `row`.
        auto it = std::upper_bound(rows.begin(), rows.begin() + lines, row);
        line = static_cast<int>(it - rows.begin()) - 1;
        if (line < 0)
            return false;
        segment = row - rows[static_cast<size_t>(line)];
        return segment < OwnRows(line);
    }

    int PaneView::TopLine() const
    {
        int line = 0, segment = 0;
        RowToLine(m_top, line, segment);
        return (std::max)(0, line);
    }

    int PaneView::BottomLine() const
    {
        int line = 0, segment = 0;
        RowToLine(m_top + VisibleRows() - 1, line, segment);
        return (std::max)(0, line);
    }

    int PaneView::CaretRow(TextPos pos) const
    {
        const int wrap = WrapColumns();
        if (wrap <= 0)
            return LineRow(pos.line);
        std::vector<int> cols;
        BuildColumns(Buffer().LineText(pos.line), m_owner.GetViewSettings().tabSize, cols);
        const int segment = (std::min)(cols[static_cast<size_t>(pos.col)] / wrap, OwnRows(pos.line) - 1);
        return LineRow(pos.line) + segment;
    }

    CPoint PaneView::PosToPoint(TextPos pos) const
    {
        std::vector<int> cols;
        BuildColumns(Buffer().LineText(pos.line), m_owner.GetViewSettings().tabSize, cols);
        pos.col = (std::max)(0, (std::min)(pos.col, static_cast<int>(cols.size()) - 1));
        int column = cols[static_cast<size_t>(pos.col)];
        int segment = 0;
        const int wrap = WrapColumns();
        if (wrap > 0)
        {
            segment = (std::min)(column / wrap, OwnRows(pos.line) - 1);
            column -= segment * wrap;
        }
        return CPoint(TextLeft() + column * m_charWidth - m_xOffset, (LineRow(pos.line) + segment - m_top) * m_lineHeight);
    }

    TextPos PaneView::PointToPos(CPoint point) const
    {
        const TextBuffer& buffer = Buffer();
        const int rowOffset = point.y >= 0 ? point.y / m_lineHeight : (point.y - m_lineHeight + 1) / m_lineHeight;
        const int row = (std::max)(0, m_top + rowOffset);
        int line = 0, segment = 0;
        if (!RowToLine(row, line, segment))
        {
            // A padding row: the end of the line before it (or the start of the file).
            if (line < 0)
                return TextPos();
            line = (std::min)(line, buffer.LineCount() - 1);
            return { line, buffer.LineLength(line) };
        }
        line = (std::min)(line, buffer.LineCount() - 1);
        const std::wstring& text = buffer.LineText(line);
        std::vector<int> cols;
        BuildColumns(text, m_owner.GetViewSettings().tabSize, cols);
        const int wrap = WrapColumns();
        int x = point.x - TextLeft() + m_xOffset;
        if (wrap > 0)
            x = (std::max)(0, (std::min)(x, wrap * m_charWidth - 1)) + segment * wrap * m_charWidth;
        const int n = static_cast<int>(text.size());
        TextPos pos = { line, n };
        for (int i = 0; i < n; ++i)
        {
            if ((cols[static_cast<size_t>(i)] + cols[static_cast<size_t>(i) + 1]) * m_charWidth / 2 > x)
            {
                pos.col = i;
                break;
            }
        }
        if (pos.col < n && IsLowSurrogate(text[static_cast<size_t>(pos.col)]))
            ++pos.col;
        return pos;
    }

    TextPos PaneView::PosFromScreen(CPoint screenPoint) const
    {
        ScreenToClient(&screenPoint);
        return PointToPos(screenPoint);
    }

    void PaneView::SetTopRow(int row, bool sync)
    {
        row = (std::max)(0, (std::min)(row, MaxTopRow()));
        if (row == m_top)
            return;
        m_top = row;
        UpdateScrollBars();
        UpdateCaretPos();
        Invalidate(FALSE);
        if (sync)
            m_owner.SyncScroll(m_pane);
        else
            m_owner.InvalidateAll();
    }

    void PaneView::CenterLine(int line, bool sync)
    {
        SetTopRow(LineRow(line) - VisibleRows() / 3, sync);
    }

    void PaneView::Refresh()
    {
        if (!m_hWnd)
            return;
        const TextBuffer& buffer = Buffer();
        // Untitled buffers (archives, formatted texts) go by their title, e.g. "main.cpp (archived)".
        std::wstring name = buffer.path;
        if (name.empty())
        {
            name = buffer.title;
            const size_t note = name.find(L" (");
            if (note != std::wstring::npos)
                name.erase(note);
        }
        m_language = m_owner.GetViewSettings().syntax ? LanguageFromPath(name) : Language::PlainText;
        m_marginWidth = GetFonts().Scale(18) + Digits(buffer.LineCount()) * m_charWidth + GetFonts().Scale(6);
        if (m_layoutRevision != m_owner.Doc().Revision())
        {
            m_layoutRevision = m_owner.Doc().Revision();
            const int tabSize = m_owner.GetViewSettings().tabSize;
            int widest = 0;
            for (const Line& line : buffer.GetLines())
            {
                // Cheap upper bound: tabs count as a full tab stop, possibly wide characters as two.
                int width = 0;
                for (wchar_t c : line.text)
                    width += c == L'\t' ? tabSize : (c < 0x1100 ? 1 : 2);
                widest = (std::max)(widest, width);
            }
            m_maxColumns = widest;
        }
        m_caret = buffer.Clamp(m_caret);
        m_anchor = buffer.Clamp(m_anchor);
        m_top = (std::min)(m_top, MaxTopRow());
        UpdateScrollBars();
        UpdateCaretPos();
        Invalidate(FALSE);
    }

    void PaneView::UpdateScrollBars()
    {
        CRect client;
        GetClientRect(&client);
        const int visible = VisibleRows();
        SCROLLINFO vertical = { sizeof(vertical), SIF_ALL | SIF_DISABLENOSCROLL, 0, MaxTopRow() + visible - 1, static_cast<UINT>(visible), m_top, 0 };
        SetScrollInfo(SB_VERT, &vertical, TRUE);

        const int page = (std::max)(1, static_cast<int>(client.Width()));
        const int contentWidth = WrapColumns() > 0 ? page : TextLeft() + m_maxColumns * m_charWidth + GetFonts().Scale(40);
        if (contentWidth <= page)
            m_xOffset = 0;
        else
            m_xOffset = (std::min)(m_xOffset, contentWidth - page);
        SCROLLINFO horizontal = { sizeof(horizontal), SIF_ALL | SIF_DISABLENOSCROLL, 0, contentWidth - 1, static_cast<UINT>(page), m_xOffset, 0 };
        SetScrollInfo(SB_HORZ, &horizontal, TRUE);
    }

    void PaneView::OnSize(UINT type, int cx, int cy)
    {
        CWnd::OnSize(type, cx, cy);
        if (WrapColumns() > 0)
            m_owner.InvalidateAll();  // the wrap width may have changed
        UpdateScrollBars();
        UpdateCaretPos();
        Invalidate(FALSE);
    }

    void PaneView::OnVScroll(UINT code, UINT, CScrollBar*)
    {
        SCROLLINFO info = { sizeof(info), SIF_ALL };
        GetScrollInfo(SB_VERT, &info);
        int top = m_top;
        switch (code)
        {
        case SB_LINEUP:
            --top;
            break;
        case SB_LINEDOWN:
            ++top;
            break;
        case SB_PAGEUP:
            top -= VisibleRows() - 1;
            break;
        case SB_PAGEDOWN:
            top += VisibleRows() - 1;
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            top = info.nTrackPos;
            break;
        case SB_TOP:
            top = 0;
            break;
        case SB_BOTTOM:
            top = TotalRows();
            break;
        default:
            return;
        }
        SetTopRow(top, true);
    }

    void PaneView::OnHScroll(UINT code, UINT, CScrollBar*)
    {
        SCROLLINFO info = { sizeof(info), SIF_ALL };
        GetScrollInfo(SB_HORZ, &info);
        int x = m_xOffset;
        switch (code)
        {
        case SB_LINELEFT:
            x -= m_charWidth * 4;
            break;
        case SB_LINERIGHT:
            x += m_charWidth * 4;
            break;
        case SB_PAGELEFT:
            x -= static_cast<int>(info.nPage);
            break;
        case SB_PAGERIGHT:
            x += static_cast<int>(info.nPage);
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            x = info.nTrackPos;
            break;
        default:
            return;
        }
        x = (std::max)(0, (std::min)(x, info.nMax - static_cast<int>(info.nPage) + 1));
        if (x != m_xOffset)
        {
            m_xOffset = x;
            UpdateScrollBars();
            UpdateCaretPos();
            Invalidate(FALSE);
        }
    }

    BOOL PaneView::OnMouseWheel(UINT flags, short delta, CPoint)
    {
        if (flags & MK_SHIFT)
        {
            OnMouseHWheel(flags, static_cast<short>(-delta), CPoint());
            return TRUE;
        }
        UINT lines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        if (lines == WHEEL_PAGESCROLL)
            lines = static_cast<UINT>(VisibleRows());
        SetTopRow(m_top - delta * static_cast<int>(lines) / WHEEL_DELTA, true);
        return TRUE;
    }

    void PaneView::OnMouseHWheel(UINT, short delta, CPoint)
    {
        if (WrapColumns() > 0)
            return;
        const int x = m_xOffset + delta * m_charWidth * 4 / WHEEL_DELTA;
        SCROLLINFO info = { sizeof(info), SIF_ALL };
        GetScrollInfo(SB_HORZ, &info);
        m_xOffset = (std::max)(0, (std::min)(x, info.nMax - static_cast<int>(info.nPage) + 1));
        UpdateScrollBars();
        UpdateCaretPos();
        Invalidate(FALSE);
    }

    // ---- painting ----

    BOOL PaneView::OnEraseBkgnd(CDC*)
    {
        return TRUE;
    }

    void PaneView::EnsureSyntaxStates(int lastLine)
    {
        const Comparison& doc = m_owner.Doc();
        if (m_statesRevision != doc.Revision())
        {
            m_statesRevision = doc.Revision();
            m_states.assign(1, 0);
        }
        const TextBuffer& buffer = Buffer();
        lastLine = (std::min)(lastLine, buffer.LineCount() - 1);
        std::vector<SyntaxSpan> spans;
        while (static_cast<int>(m_states.size()) <= lastLine + 1)
        {
            const int line = static_cast<int>(m_states.size()) - 1;
            m_states.push_back(HighlightLine(m_language, buffer.LineText(line), m_states.back(), spans));
        }
    }

    CRect PaneView::MergeButtonRect(int y) const
    {
        CRect client;
        GetClientRect(&client);
        const int size = (std::min)(m_lineHeight - 2, GetFonts().Scale(18));
        const int right = client.right - GetFonts().Scale(4);
        const int top = y + (m_lineHeight - size) / 2;
        return CRect(right - size - GetFonts().Scale(4), top, right, top + size);
    }

    int PaneView::MergeTarget() const
    {
        if (m_owner.Doc().IsThreeWay())
            return m_pane == 1 ? -1 : 1;
        return 1 - m_pane;
    }

    bool PaneView::ShowsMergeButton(const Chunk& chunk) const
    {
        const int target = MergeTarget();
        const Comparison& doc = m_owner.Doc();
        return target >= 0 && chunk.IsDifference() && doc.PaneDiffersInChunk(chunk, m_pane) && doc.PaneDiffersInChunk(chunk, target) &&
            !doc.Buffer(target).readOnly;
    }

    int PaneView::MergeButtonAt(CPoint point) const
    {
        const std::vector<Chunk>& chunks = m_owner.Doc().Chunks();
        CRect client;
        GetClientRect(&client);
        for (size_t k = 0; k < chunks.size(); ++k)
        {
            const int top = ChunkTopY(static_cast<int>(k));
            const int bottom = ChunkBottomY(static_cast<int>(k));
            if (bottom < -m_lineHeight || top > client.bottom)
                continue;
            if (!ShowsMergeButton(chunks[k]))
                continue;
            const int y = top - (top == bottom ? m_lineHeight / 2 : 0);
            if (MergeButtonRect(y).PtInRect(point))
                return static_cast<int>(k);
        }
        return -1;
    }

    void PaneView::PaintLine(CDC& dc, int line, int y, int width, ChunkStyle style, const std::vector<CharRange>* ranges)
    {
        const Theme& theme = GetTheme();
        const ViewSettings& settings = m_owner.GetViewSettings();
        const Line& lineData = Buffer().GetLine(line);
        const std::wstring& text = lineData.text;
        const int n = static_cast<int>(text.size());
        std::vector<int> cols;
        BuildColumns(text, settings.tabSize, cols);
        const COLORREF back = theme.StyleBack(style);
        const int textLeft = TextLeft();
        const int wrap = WrapColumns();
        const int segments = OwnRows(line);
        const bool resolved = (lineData.flags & LineResolved) != 0;

        // Per-character colours from the syntax highlighter.
        std::vector<COLORREF> colors(static_cast<size_t>(n), theme.text);
        if (m_language != Language::PlainText && line < static_cast<int>(m_states.size()))
        {
            HighlightLine(m_language, text, m_states[static_cast<size_t>(line)], m_spans);
            for (const SyntaxSpan& span : m_spans)
            {
                const COLORREF color = theme.TokenColor(span.cls);
                for (int i = span.start; i < span.start + span.length && i < n; ++i)
                    colors[static_cast<size_t>(i)] = color;
            }
        }

        // Display text: tabs become spaces (or an arrow), each character gets its column width.
        std::wstring display;
        std::vector<int> dx;
        std::vector<int> displayStart(static_cast<size_t>(n) + 1);
        display.reserve(static_cast<size_t>(n) + 8);
        for (int i = 0; i < n; ++i)
        {
            displayStart[static_cast<size_t>(i)] = static_cast<int>(display.size());
            const wchar_t c = text[static_cast<size_t>(i)];
            const int columns = cols[static_cast<size_t>(i) + 1] - cols[static_cast<size_t>(i)];
            if (c == L'\t')
            {
                for (int k = 0; k < columns; ++k)
                {
                    display.push_back(k == 0 && settings.showWhitespace ? L'\x2192' : L' ');
                    dx.push_back(m_charWidth);
                }
                if (settings.showWhitespace)
                    colors[static_cast<size_t>(i)] = theme.whitespace;
            }
            else if (c == L' ' && settings.showWhitespace)
            {
                display.push_back(L'\x00B7');
                dx.push_back(m_charWidth);
                colors[static_cast<size_t>(i)] = theme.whitespace;
            }
            else
            {
                display.push_back(c < 0x20 ? static_cast<wchar_t>(0x2400 + c) : c);
                dx.push_back(columns * m_charWidth);
            }
        }
        displayStart[static_cast<size_t>(n)] = static_cast<int>(display.size());
        if (resolved)
        {
            for (COLORREF& color : colors)
                color = Blend(color, back, 60);
        }

        TextPos selFrom, selTo;
        const bool selected = HasSelection();
        if (selected)
            GetSelection(selFrom, selTo);
        TEXTMETRICW tm = {};
        dc.GetTextMetrics(&tm);
        dc.SetBkMode(TRANSPARENT);

        for (int segment = 0; segment < segments; ++segment)
        {
            const int rowY = y + segment * m_lineHeight;
            if (rowY + m_lineHeight < 0)
                continue;
            if (rowY > 32000)
                break;
            // Columns of this segment, and where column 0 is drawn.
            const int segStart = wrap > 0 ? segment * wrap : 0;
            const int segEnd = wrap > 0 && segment + 1 < segments ? (segment + 1) * wrap : INT_MAX / 2;
            const int left = textLeft - m_xOffset - segStart * m_charWidth;
            auto colToX = [&](int column) { return left + column * m_charWidth; };
            auto fillColumns = [&](int c0, int c1, int top, int height, COLORREF color) {
                c0 = (std::max)(c0, segStart);
                c1 = (std::min)(c1, segEnd);
                const int x0 = (std::max)(textLeft, colToX(c0));
                const int x1 = (std::min)(width, colToX(c1));
                if (x1 > x0 && c1 > c0)
                    dc.FillSolidRect(x0, top, x1 - x0, height, color);
            };

            dc.FillSolidRect(0, rowY, width, m_lineHeight, back);
            if (ranges)
            {
                for (const CharRange& range : *ranges)
                {
                    const int end = (std::min)(n, range.start + range.length);
                    fillColumns(cols[static_cast<size_t>(range.start)], cols[static_cast<size_t>(end)], rowY + 1, m_lineHeight - 2, theme.StyleChar(style));
                }
            }
            if (selected && line >= selFrom.line && line <= selTo.line)
            {
                const int c0 = line == selFrom.line ? cols[static_cast<size_t>(selFrom.col)] : 0;
                const int c1 = line == selTo.line ? cols[static_cast<size_t>(selTo.col)] : cols[static_cast<size_t>(n)] + 1;
                fillColumns(c0, c1, rowY, m_lineHeight, m_hasFocus ? theme.selection : theme.selectionInactive);
            }

            CRect clip(textLeft, rowY, width, rowY + m_lineHeight);
            const int textY = rowY + (m_lineHeight - tm.tmHeight) / 2;
            int i = 0;
            while (i < n && (cols[static_cast<size_t>(i) + 1] <= segStart || colToX(cols[static_cast<size_t>(i) + 1]) < textLeft))
                ++i;
            while (i < n && cols[static_cast<size_t>(i)] < segEnd)
            {
                const COLORREF color = colors[static_cast<size_t>(i)];
                int j = i + 1;
                while (j < n && colors[static_cast<size_t>(j)] == color && cols[static_cast<size_t>(j)] < segEnd)
                    ++j;
                const int x = colToX(cols[static_cast<size_t>(i)]);
                if (x > width)
                    break;
                dc.SetTextColor(color);
                const int start = displayStart[static_cast<size_t>(i)];
                const int length = displayStart[static_cast<size_t>(j)] - start;
                ::ExtTextOutW(dc.m_hDC, x, textY, ETO_CLIPPED, &clip, display.c_str() + start, static_cast<UINT>(length), dx.data() + start);
                i = j;
            }

            if (segment + 1 == segments && settings.showWhitespace && lineData.eol != Eol::None)
            {
                CFont* old = dc.SelectObject(&GetFonts().uiSmall);
                dc.SetTextColor(theme.whitespace);
                const int x = colToX(cols[static_cast<size_t>(n)]) + 2;
                CRect eolRect(x, rowY, x + GetFonts().Scale(40), rowY + m_lineHeight);
                if (eolRect.left >= textLeft)
                    dc.DrawText(EolName(lineData.eol), -1, &eolRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                dc.SelectObject(old);
            }

            // Line number margin (keeps the block tint): edit marker, bookmark, number.
            dc.FillSolidRect(0, rowY, textLeft, m_lineHeight, style == ChunkStyle::None ? theme.lineNumberBack : back);
            if (lineData.flags & LineEdited)
                dc.FillSolidRect(GetFonts().Scale(2), rowY, GetFonts().Scale(3), m_lineHeight, theme.editMarker);
            if (segment == 0)
            {
                if (lineData.flags & LineBookmark)
                {
                    const int size = (std::min)(GetFonts().Scale(10), m_lineHeight - 4);
                    CRect mark(GetFonts().Scale(7), rowY + (m_lineHeight - size) / 2, GetFonts().Scale(7) + size, rowY + (m_lineHeight + size) / 2);
                    CBrush brush(theme.bookmark);
                    CPen pen(PS_SOLID, 1, theme.bookmark);
                    CBrush* ob = dc.SelectObject(&brush);
                    CPen* op = dc.SelectObject(&pen);
                    dc.RoundRect(&mark, CPoint(GetFonts().Scale(3), GetFonts().Scale(3)));
                    dc.SelectObject(ob);
                    dc.SelectObject(op);
                }
                CString number;
                number.Format(L"%d", line + 1);
                dc.SetTextColor(resolved ? theme.textResolved : theme.lineNumber);
                CRect numberRect(0, rowY, m_marginWidth, rowY + m_lineHeight);
                dc.DrawText(number, &numberRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

                // A bookmark comment: a note tag at the right edge (its first line).
                if ((lineData.flags & LineBookmark) && !lineData.comment.empty())
                {
                    CFont* oldFont = dc.SelectObject(&GetFonts().uiSmall);
                    const std::wstring note = L"\x270E " + lineData.comment.substr(0, lineData.comment.find(L'\n'));
                    CRect measure(0, 0, 0, 0);
                    dc.DrawText(note.c_str(), -1, &measure, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
                    const int inset = GetFonts().Scale(6);
                    const int tagWidth = (std::min)(static_cast<int>(measure.Width()) + inset * 2, (width - textLeft) * 2 / 5);
                    CRect tag(width - tagWidth - inset, rowY + 1, width - inset, rowY + m_lineHeight - 1);
                    if (tag.left > textLeft + inset)
                    {
                        CBrush brush(RGB(255, 244, 196));
                        CPen pen(PS_SOLID, 1, RGB(222, 190, 92));
                        CBrush* ob = dc.SelectObject(&brush);
                        CPen* op = dc.SelectObject(&pen);
                        dc.RoundRect(&tag, CPoint(GetFonts().Scale(6), GetFonts().Scale(6)));
                        dc.SelectObject(ob);
                        dc.SelectObject(op);
                        dc.SetTextColor(RGB(112, 84, 0));
                        CRect textRect(tag.left + inset, tag.top, tag.right - inset / 2, tag.bottom);
                        dc.DrawText(note.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                    }
                    dc.SelectObject(oldFont);
                }
            }
            else
            {
                // Continuation of a wrapped line.
                dc.SetTextColor(theme.whitespace);
                CRect markRect(0, rowY, m_marginWidth, rowY + m_lineHeight);
                dc.DrawText(L"\x21AA", 1, &markRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
        }
    }

    void PaneView::OnPaint()
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
        CFont* oldFont = dc.SelectObject(&GetFonts().code);

        const Theme& theme = GetTheme();
        const Comparison& doc = m_owner.Doc();
        const TextBuffer& buffer = Buffer();
        const int width = client.Width();

        // Rows inside the file start as padding; lines paint over their own rows.
        const int endY = (TotalRows() - m_top) * m_lineHeight;
        dc.FillSolidRect(&client, theme.paneBack);
        if (endY > 0)
        {
            const int padBottom = (std::min)(endY, static_cast<int>(client.bottom));
            dc.FillSolidRect(0, 0, width, padBottom, theme.padding);
            CBrush hatch(HS_BDIAGONAL, Blend(theme.padding, theme.chromeBorder, 60));
            dc.SetBkColor(theme.padding);
            CRect padRect(0, 0, width, padBottom);
            dc.FillRect(&padRect, &hatch);
        }

        int firstLine = 0, lastLine = 0, segment = 0;
        RowToLine(m_top, firstLine, segment);
        RowToLine(m_top + client.Height() / m_lineHeight + 1, lastLine, segment);
        firstLine = (std::max)(0, firstLine);
        lastLine = (std::max)(0, (std::min)(lastLine, buffer.LineCount() - 1));
        if (m_language != Language::PlainText)
            EnsureSyntaxStates(lastLine);

        struct Box
        {
            int chunk;
            ChunkStyle style;
        };
        std::vector<Box> boxes;
        const std::vector<Chunk>& chunks = doc.Chunks();
        const int reference = doc.ReferencePane();
        const int pane = m_pane;
        auto firstChunk = std::lower_bound(chunks.begin(), chunks.end(), firstLine, [pane](const Chunk& chunk, int line) { return chunk.End(pane) < line; });
        std::vector<char> painted(static_cast<size_t>(lastLine - firstLine + 1), 0);
        for (auto it = firstChunk; it != chunks.end(); ++it)
        {
            const Chunk& chunk = *it;
            if (chunk.start[pane] > lastLine + 1)
                break;
            const int index = static_cast<int>(it - chunks.begin());
            const ChunkStyle style = doc.StyleInPane(chunk, pane);
            if (style != ChunkStyle::None)
                boxes.push_back({ index, style });
            if (chunk.count[pane] == 0)
                continue;
            const ChunkDetail* detail = (style == ChunkStyle::Change || style == ChunkStyle::Conflict) ? &m_owner.Detail(index, pane) : nullptr;
            for (int line = (std::max)(firstLine, chunk.start[pane]); line <= (std::min)(lastLine, chunk.End(pane) - 1); ++line)
            {
                ChunkStyle lineStyle = style;
                const std::vector<CharRange>* ranges = nullptr;
                if (detail)
                {
                    const size_t rel = static_cast<size_t>(line - chunk.start[pane]);
                    if (rel < detail->paired.size() && detail->paired[rel])
                        ranges = &detail->ranges[rel];
                    else if (style == ChunkStyle::Change)
                        lineStyle = pane == reference ? ChunkStyle::Removal : ChunkStyle::Insertion;
                }
                PaintLine(dc, line, LineToY(line), width, lineStyle, ranges);
                painted[static_cast<size_t>(line - firstLine)] = 1;
            }
        }
        for (int line = firstLine; line <= lastLine; ++line)
        {
            if (!painted[static_cast<size_t>(line - firstLine)])
                PaintLine(dc, line, LineToY(line), width, ChunkStyle::None, nullptr);
        }

        // Synchronization links: a dotted line above the linked line.
        for (const SyncPoint& point : doc.SyncPoints())
        {
            const int y = LineToY(point.line[pane]);
            if (y < -2 || y > client.bottom)
                continue;
            for (int x = 0; x < width; x += GetFonts().Scale(6))
                dc.FillSolidRect(x, y, GetFonts().Scale(3), (std::max)(1, GetFonts().Scale(2)), theme.syncLink);
        }

        // Block borders, insertion points, merge buttons.
        const int current = m_owner.CurrentChunk();
        for (const Box& box : boxes)
        {
            const bool isCurrent = box.chunk == current;
            const int thickness = isCurrent ? GetFonts().Scale(2) : 1;
            const COLORREF color = isCurrent ? Blend(theme.StyleLine(box.style), RGB(0, 0, 0), 25) : theme.StyleLine(box.style);
            const int yTop = ChunkTopY(box.chunk);
            const int yBottom = ChunkBottomY(box.chunk);
            if (yTop == yBottom)
            {
                dc.FillSolidRect(0, yTop - (thickness + 1) / 2, width, thickness + 1, color);
            }
            else
            {
                dc.FillSolidRect(0, yTop, width, thickness, color);
                dc.FillSolidRect(0, yBottom - thickness, width, thickness, color);
            }
            const Chunk& chunk = chunks[static_cast<size_t>(box.chunk)];
            if (ShowsMergeButton(chunk))
            {
                const int y = yTop - (yTop == yBottom ? m_lineHeight / 2 : 0);
                CRect button = MergeButtonRect(y);
                CBrush brush(RGB(255, 255, 255));
                CPen pen(PS_SOLID, 1, theme.StyleLine(box.style));
                CBrush* ob = dc.SelectObject(&brush);
                CPen* op = dc.SelectObject(&pen);
                dc.RoundRect(&button, CPoint(GetFonts().Scale(4), GetFonts().Scale(4)));
                dc.SelectObject(ob);
                dc.SelectObject(op);
                DrawGlyph(dc, button, MergeTarget() > m_pane ? 0xE72A : 0xE72B, theme.chromeText, GetFonts().iconsSmall);
            }
        }

        // Where dragged text would land.
        if (m_dropPos.line >= 0)
        {
            const CPoint point = PosToPoint(m_dropPos);
            dc.FillSolidRect(point.x - 1, point.y, GetFonts().Scale(2), m_lineHeight, theme.dropCaret);
        }

        dc.SelectObject(oldFont);
        paint.BitBlt(0, 0, client.Width(), client.Height(), &dc, 0, 0, SRCCOPY);
        dc.SelectObject(oldBitmap);
    }

    // ---- caret and selection ----

    void PaneView::GetSelection(TextPos& from, TextPos& to) const
    {
        from = (std::min)(m_caret, m_anchor);
        to = m_caret < m_anchor ? m_anchor : m_caret;
    }

    bool PaneView::InSelection(TextPos pos) const
    {
        if (!HasSelection())
            return false;
        TextPos from, to;
        GetSelection(from, to);
        return from <= pos && pos < to;
    }

    void PaneView::SetCaret(TextPos pos, bool extend, bool ensureVisible)
    {
        pos = Buffer().Clamp(pos);
        m_caret = pos;
        if (!extend)
            m_anchor = pos;
        if (ensureVisible)
            EnsureCaretVisible();
        UpdateCaretPos();
        Invalidate(FALSE);  // selection and current-change highlight
        m_owner.OnCaretMoved(m_pane);
    }

    void PaneView::SetSelection(TextPos anchor, TextPos caret, bool ensureVisible)
    {
        m_anchor = Buffer().Clamp(anchor);
        SetCaret(caret, true, ensureVisible);
    }

    void PaneView::SelectAll()
    {
        SetSelection(TextPos(), Buffer().EndPos(), false);
    }

    void PaneView::SetDropPos(TextPos pos)
    {
        if (pos != m_dropPos)
        {
            m_dropPos = pos;
            Invalidate(FALSE);
        }
    }

    void PaneView::SelectedLines(int& first, int& last) const
    {
        TextPos from, to;
        GetSelection(from, to);
        first = from.line;
        last = to.line;
        if (to.col == 0 && to.line > from.line)
            --last;
    }

    void PaneView::EnsureCaretVisible(bool sync)
    {
        const int visible = VisibleRows();
        const int row = CaretRow(m_caret);
        if (row < m_top)
            SetTopRow(row, sync);
        else if (row >= m_top + visible)
            SetTopRow(row - visible + 1, sync);

        if (WrapColumns() > 0)
            return;
        CRect client;
        GetClientRect(&client);
        const int x = PosToPoint(m_caret).x + m_xOffset - TextLeft();
        const int area = client.Width() - TextLeft() - GetFonts().Scale(30);
        int offset = m_xOffset;
        if (x < offset)
            offset = (std::max)(0, x - m_charWidth * 8);
        else if (area > 0 && x > offset + area)
            offset = x - area + m_charWidth * 8;
        if (offset != m_xOffset)
        {
            m_xOffset = offset;
            UpdateScrollBars();
            Invalidate(FALSE);
        }
    }

    void PaneView::UpdateCaretPos()
    {
        if (!m_hasFocus)
            return;
        const CPoint point = PosToPoint(m_caret);
        CRect client;
        GetClientRect(&client);
        if (point.y < 0 || point.y > client.bottom || point.x < TextLeft())
            SetCaretPos(CPoint(-100, -100));
        else
            SetCaretPos(point);
    }

    void PaneView::OnSetFocus(CWnd* oldWnd)
    {
        CWnd::OnSetFocus(oldWnd);
        m_hasFocus = true;
        CreateSolidCaret(GetFonts().Scale(2), m_lineHeight);
        UpdateCaretPos();
        ShowCaret();
        m_owner.SetActivePane(m_pane);
        Invalidate(FALSE);
    }

    void PaneView::OnKillFocus(CWnd* newWnd)
    {
        CWnd::OnKillFocus(newWnd);
        m_hasFocus = false;
        HideCaret();
        DestroyCaret();
        Invalidate(FALSE);
    }

    LRESULT PaneView::OnImeStartComposition(WPARAM wParam, LPARAM lParam)
    {
        HIMC imc = ImmGetContext(m_hWnd);
        if (imc)
        {
            COMPOSITIONFORM form = {};
            form.dwStyle = CFS_POINT;
            form.ptCurrentPos = PosToPoint(m_caret);
            ImmSetCompositionWindow(imc, &form);
            LOGFONTW lf = {};
            GetFonts().code.GetLogFont(&lf);
            ImmSetCompositionFontW(imc, &lf);
            ImmReleaseContext(m_hWnd, imc);
        }
        return DefWindowProc(WM_IME_STARTCOMPOSITION, wParam, lParam);
    }

    UINT PaneView::OnGetDlgCode()
    {
        return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTCHARS | DLGC_WANTTAB;
    }

    // ---- mouse ----

    BOOL PaneView::OnSetCursor(CWnd* wnd, UINT hitTest, UINT message)
    {
        if (hitTest == HTCLIENT)
        {
            CPoint point;
            GetCursorPos(&point);
            ScreenToClient(&point);
            if (MergeButtonAt(point) >= 0)
            {
                SetCursor(::LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
            if (point.x < TextLeft() || (!m_dragging && InSelection(PointToPos(point))))
            {
                SetCursor(::LoadCursor(nullptr, IDC_ARROW));
                return TRUE;
            }
        }
        return CWnd::OnSetCursor(wnd, hitTest, message);
    }

    void PaneView::OnLButtonDown(UINT flags, CPoint point)
    {
        SetFocus();
        const int chunk = MergeButtonAt(point);
        if (chunk >= 0)
        {
            m_owner.CopyChunk(chunk, m_pane, MergeTarget());
            return;
        }
        const TextPos pos = PointToPos(point);
        if (point.x >= TextLeft() && !(flags & MK_SHIFT) && InSelection(pos))
        {
            // Maybe the start of dragging the selected text; a plain click if the mouse doesn't move.
            m_dragPending = true;
            m_dragStart = point;
            SetCapture();
            return;
        }
        if (point.x < TextLeft())
        {
            // Clicking a line number selects the line.
            const TextPos lineStart = { pos.line, 0 };
            const TextPos nextLine = pos.line + 1 < Buffer().LineCount() ? TextPos{ pos.line + 1, 0 } : TextPos{ pos.line, Buffer().LineLength(pos.line) };
            if (flags & MK_SHIFT)
                SetCaret(m_anchor < lineStart ? nextLine : lineStart, true);
            else
                SetSelection(lineStart, nextLine);
        }
        else
        {
            SetCaret(pos, (flags & MK_SHIFT) != 0);
        }
        m_desiredCol = -1;
        m_dragging = true;
        SetCapture();
        SetTimer(kDragTimer, 50, nullptr);
    }

    void PaneView::OnLButtonDblClk(UINT, CPoint point)
    {
        const TextPos pos = PointToPos(point);
        const std::wstring& text = Buffer().LineText(pos.line);
        int start = pos.col, end = pos.col;
        if (start < static_cast<int>(text.size()) && IsWordChar(text[static_cast<size_t>(start)]))
        {
            while (start > 0 && IsWordChar(text[static_cast<size_t>(start) - 1]))
                --start;
            while (end < static_cast<int>(text.size()) && IsWordChar(text[static_cast<size_t>(end)]))
                ++end;
        }
        else if (end < static_cast<int>(text.size()))
        {
            ++end;
        }
        SetSelection({ pos.line, start }, { pos.line, end });
    }

    void PaneView::OnLButtonUp(UINT flags, CPoint point)
    {
        if (m_textDragging)
        {
            m_textDragging = false;
            m_dragPending = false;
            CPoint screen = point;
            ClientToScreen(&screen);
            ReleaseCapture();
            m_owner.EndTextDrag(m_pane, screen, (flags & MK_CONTROL) != 0, true);
            return;
        }
        if (m_dragPending)
        {
            m_dragPending = false;
            ReleaseCapture();
            SetCaret(PointToPos(point), false);
            return;
        }
        if (m_dragging)
        {
            m_dragging = false;
            KillTimer(kDragTimer);
            ReleaseCapture();
        }
    }

    void PaneView::OnCaptureChanged(CWnd* wnd)
    {
        if (m_textDragging && wnd != this)
        {
            m_textDragging = false;
            m_owner.EndTextDrag(m_pane, CPoint(), false, false);
        }
        m_dragPending = false;
        if (m_dragging && wnd != this)
        {
            m_dragging = false;
            KillTimer(kDragTimer);
        }
        CWnd::OnCaptureChanged(wnd);
    }

    void PaneView::OnMouseMove(UINT flags, CPoint point)
    {
        if (m_dragPending && (flags & MK_LBUTTON))
        {
            const int threshold = GetSystemMetrics(SM_CXDRAG);
            if (abs(point.x - m_dragStart.x) > threshold || abs(point.y - m_dragStart.y) > threshold)
            {
                m_dragPending = false;
                m_textDragging = true;
            }
        }
        if (m_textDragging)
        {
            CPoint screen = point;
            ClientToScreen(&screen);
            m_owner.UpdateTextDrag(m_pane, screen, (flags & MK_CONTROL) != 0);
            return;
        }
        if (m_dragging && (flags & MK_LBUTTON))
            SetCaret(PointToPos(point), true, true);
    }

    void PaneView::OnTimer(UINT_PTR id)
    {
        if (id != kDragTimer || !m_dragging)
        {
            CWnd::OnTimer(id);
            return;
        }
        CPoint point;
        GetCursorPos(&point);
        ScreenToClient(&point);
        CRect client;
        GetClientRect(&client);
        if (point.y < 0)
            SetTopRow(m_top - 1, true);
        else if (point.y > client.bottom)
            SetTopRow(m_top + 1, true);
        else
            return;
        SetCaret(PointToPos(point), true, false);
    }

    void PaneView::OnRButtonDown(UINT, CPoint point)
    {
        SetFocus();
        const TextPos pos = PointToPos(point);
        TextPos from, to;
        GetSelection(from, to);
        if (!HasSelection() || pos < from || to < pos)
            SetCaret(pos, false);
    }

    void PaneView::OnContextMenu(CWnd*, CPoint point)
    {
        if (point.x == -1 && point.y == -1)
        {
            point = PosToPoint(m_caret);
            point.y += m_lineHeight;
            ClientToScreen(&point);
        }
        m_owner.ShowPaneMenu(m_pane, point);
    }

    // ---- keyboard ----

    void PaneView::MoveCaret(UINT key, bool extend, bool word)
    {
        const TextBuffer& buffer = Buffer();
        TextPos pos = m_caret;
        const std::wstring& text = buffer.LineText(pos.line);
        const int tabSize = m_owner.GetViewSettings().tabSize;
        bool keepDesired = false;
        switch (key)
        {
        case VK_LEFT:
            if (!extend && HasSelection())
            {
                TextPos from, to;
                GetSelection(from, to);
                pos = from;
                break;
            }
            if (pos.col == 0)
            {
                if (pos.line > 0)
                    pos = { pos.line - 1, buffer.LineLength(pos.line - 1) };
                break;
            }
            if (word)
            {
                while (pos.col > 0 && iswspace(text[static_cast<size_t>(pos.col) - 1]))
                    --pos.col;
                const bool wordChar = pos.col > 0 && IsWordChar(text[static_cast<size_t>(pos.col) - 1]);
                while (pos.col > 0 && !iswspace(text[static_cast<size_t>(pos.col) - 1]) && IsWordChar(text[static_cast<size_t>(pos.col) - 1]) == wordChar)
                    --pos.col;
            }
            else
            {
                --pos.col;
                if (pos.col > 0 && IsLowSurrogate(text[static_cast<size_t>(pos.col)]))
                    --pos.col;
            }
            break;
        case VK_RIGHT:
        {
            if (!extend && HasSelection())
            {
                TextPos from, to;
                GetSelection(from, to);
                pos = to;
                break;
            }
            const int n = static_cast<int>(text.size());
            if (pos.col >= n)
            {
                if (pos.line + 1 < buffer.LineCount())
                    pos = { pos.line + 1, 0 };
                break;
            }
            if (word)
            {
                const bool wordChar = IsWordChar(text[static_cast<size_t>(pos.col)]);
                while (pos.col < n && !iswspace(text[static_cast<size_t>(pos.col)]) && IsWordChar(text[static_cast<size_t>(pos.col)]) == wordChar)
                    ++pos.col;
                while (pos.col < n && iswspace(text[static_cast<size_t>(pos.col)]))
                    ++pos.col;
            }
            else
            {
                ++pos.col;
                if (pos.col < n && IsLowSurrogate(text[static_cast<size_t>(pos.col)]))
                    ++pos.col;
            }
            break;
        }
        case VK_UP:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT:
        {
            if (word && (key == VK_UP || key == VK_DOWN))
            {
                SetTopRow(m_top + (key == VK_UP ? -1 : 1), true);
                return;
            }
            // Move by display rows (wrapped segments), skipping padding rows.
            const int wrap = WrapColumns();
            std::vector<int> cols;
            BuildColumns(text, tabSize, cols);
            const int row = CaretRow(pos);
            const int segment = row - LineRow(pos.line);
            if (m_desiredCol < 0)
                m_desiredCol = cols[static_cast<size_t>(pos.col)] - segment * (wrap > 0 ? wrap : 0);
            const int page = (std::max)(1, VisibleRows() - 1);
            const int delta = key == VK_UP ? -1 : key == VK_DOWN ? 1 : key == VK_PRIOR ? -page : page;
            const int step = delta < 0 ? -1 : 1;
            int target = (std::max)(0, (std::min)(row + delta, TotalRows() - 1));
            int line = 0, targetSegment = 0;
            while (!RowToLine(target, line, targetSegment) && target + step >= 0 && target + step < TotalRows())
                target += step;
            if (!RowToLine(target, line, targetSegment))
            {
                // Only padding that way: go back towards the caret.
                while (!RowToLine(target, line, targetSegment) && target != row)
                    target -= step;
            }
            line = (std::max)(0, (std::min)(line, buffer.LineCount() - 1));
            if (key == VK_PRIOR || key == VK_NEXT)
                SetTopRow(m_top + delta, true);
            const std::wstring& targetText = buffer.LineText(line);
            BuildColumns(targetText, tabSize, cols);
            const int column = targetSegment * (wrap > 0 ? wrap : 0) + m_desiredCol;
            const int segmentEnd = wrap > 0 && targetSegment + 1 < OwnRows(line) ? (targetSegment + 1) * wrap : INT_MAX;
            pos = { line, static_cast<int>(targetText.size()) };
            for (size_t i = 0; i < targetText.size(); ++i)
            {
                if (cols[i + 1] > column || cols[i + 1] >= segmentEnd)
                {
                    pos.col = static_cast<int>(i);
                    break;
                }
            }
            keepDesired = true;
            break;
        }
        case VK_HOME:
            if (word)
            {
                pos = TextPos();
            }
            else
            {
                // Smart home: first non-blank character, then column 0.
                int firstText = 0;
                while (firstText < static_cast<int>(text.size()) && (text[static_cast<size_t>(firstText)] == L' ' || text[static_cast<size_t>(firstText)] == L'\t'))
                    ++firstText;
                pos.col = pos.col == firstText ? 0 : firstText;
            }
            break;
        case VK_END:
            pos = word ? buffer.EndPos() : TextPos{ pos.line, static_cast<int>(text.size()) };
            break;
        default:
            return;
        }
        const int desired = m_desiredCol;
        SetCaret(pos, extend);
        m_desiredCol = keepDesired ? desired : -1;
    }

    void PaneView::OnKeyDown(UINT key, UINT repeat, UINT flags)
    {
        const bool shift = GetKeyState(VK_SHIFT) < 0;
        const bool ctrl = GetKeyState(VK_CONTROL) < 0;
        switch (key)
        {
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_HOME:
        case VK_END:
            MoveCaret(key, shift, ctrl);
            return;
        case VK_DELETE:
            if (!CanEdit())
            {
                MessageBeep(MB_OK);
                return;
            }
            if (!HasSelection())
            {
                const TextPos before = m_caret;
                MoveCaret(VK_RIGHT, true, ctrl);
                if (m_caret == before)
                    return;
            }
            InsertText(L"", m_caret.line == m_anchor.line);
            return;
        case VK_ESCAPE:
            if (m_textDragging)
            {
                ReleaseCapture();
                return;
            }
            SetCaret(m_caret, false, false);
            return;
        default:
            break;
        }
        CWnd::OnKeyDown(key, repeat, flags);
    }

    void PaneView::OnChar(UINT ch, UINT, UINT)
    {
        if (GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_MENU) >= 0)
            return;  // Ctrl+letter shortcuts arrive as control characters
        if (!CanEdit())
        {
            MessageBeep(MB_OK);
            return;
        }
        switch (ch)
        {
        case L'\b':
            if (!HasSelection())
            {
                if (m_caret == TextPos())
                    return;
                MoveCaret(VK_LEFT, true, false);
            }
            InsertText(L"", m_caret.line == m_anchor.line);
            return;
        case L'\r':
        {
            // New line with the current line's indentation.
            const std::wstring& text = Buffer().LineText(m_caret.line);
            size_t indent = 0;
            while (indent < text.size() && indent < static_cast<size_t>(m_caret.col) && (text[indent] == L' ' || text[indent] == L'\t'))
                ++indent;
            InsertText(L"\n" + text.substr(0, indent));
            return;
        }
        case L'\t':
        {
            int first, last;
            SelectedLines(first, last);
            if (HasSelection() && (last > first || m_caret.line != m_anchor.line))
                IndentSelection(GetKeyState(VK_SHIFT) < 0);
            else if (GetKeyState(VK_SHIFT) < 0)
                IndentSelection(true);
            else
                InsertText(L"\t", true);
            return;
        }
        default:
            if (ch < 0x20 || ch == 0x7F)
                return;
            InsertText(std::wstring(1, static_cast<wchar_t>(ch)), true);
            return;
        }
    }

    // ---- editing ----

    bool PaneView::CanEdit() const
    {
        return !Buffer().readOnly;
    }

    void PaneView::InsertText(const std::wstring& text, bool typing)
    {
        if (!CanEdit())
        {
            MessageBeep(MB_OK);
            return;
        }
        TextPos from, to;
        GetSelection(from, to);
        const TextPos end = m_owner.Doc().ReplaceRange(m_pane, from, to, text, typing);
        m_caret = m_anchor = end;
        m_desiredCol = -1;
        m_owner.OnTextChanged();
        EnsureCaretVisible();
        UpdateCaretPos();
        m_owner.OnCaretMoved(m_pane);
    }

    void PaneView::DeleteSelection()
    {
        if (HasSelection())
            InsertText(L"");
    }

    std::wstring PaneView::SelectedText() const
    {
        TextPos from, to;
        GetSelection(from, to);
        return Buffer().GetText(from, to);
    }

    void PaneView::Copy()
    {
        std::wstring text;
        if (HasSelection())
        {
            text = SelectedText();
        }
        else
        {
            // No selection: copy the whole line.
            const Line& line = Buffer().GetLine(m_caret.line);
            text = line.text + (line.eol == Eol::None ? EolChars(Buffer().eol) : EolChars(line.eol));
        }
        if (!OpenClipboard())
            return;
        EmptyClipboard();
        const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory)
        {
            void* data = GlobalLock(memory);
            memcpy(data, text.c_str(), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory))
                GlobalFree(memory);
        }
        CloseClipboard();
    }

    void PaneView::Cut()
    {
        if (!CanEdit())
            return;
        if (!HasSelection())
        {
            // No selection: cut the whole line.
            const TextPos lineStart = { m_caret.line, 0 };
            const TextPos next = m_caret.line + 1 < Buffer().LineCount() ? TextPos{ m_caret.line + 1, 0 } : TextPos{ m_caret.line, Buffer().LineLength(m_caret.line) };
            Copy();
            SetSelection(lineStart, next, false);
            InsertText(L"");
            return;
        }
        Copy();
        InsertText(L"");
    }

    void PaneView::Paste()
    {
        if (!CanEdit() || !IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard())
            return;
        std::wstring text;
        if (HANDLE data = GetClipboardData(CF_UNICODETEXT))
        {
            if (const wchar_t* chars = static_cast<const wchar_t*>(GlobalLock(data)))
            {
                text = chars;
                GlobalUnlock(data);
            }
        }
        CloseClipboard();
        if (!text.empty())
            InsertText(text);
    }

    void PaneView::IndentSelection(bool outdent)
    {
        if (!CanEdit())
            return;
        int first, last;
        SelectedLines(first, last);
        bool useTabs = false;
        for (const Line& line : Buffer().GetLines())
        {
            if (!line.text.empty() && (line.text[0] == L'\t' || line.text[0] == L' '))
            {
                useTabs = line.text[0] == L'\t';
                break;
            }
        }
        const bool hadSelection = HasSelection();
        const int oldLength = Buffer().LineLength(m_caret.line);
        m_owner.Doc().Indent(m_pane, first, last, outdent, m_owner.GetViewSettings().tabSize, useTabs);
        if (hadSelection)
        {
            m_anchor = { first, 0 };
            m_caret = { last, Buffer().LineLength(last) };
        }
        else
        {
            m_caret.col = (std::max)(0, m_caret.col + Buffer().LineLength(m_caret.line) - oldLength);
            m_anchor = m_caret;
        }
        m_owner.OnTextChanged();
        EnsureCaretVisible();
        m_owner.OnCaretMoved(m_pane);
    }
}
