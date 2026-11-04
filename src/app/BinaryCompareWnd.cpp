// BinaryCompareWnd.cpp - hex panes and the byte comparison tab.
#include "BinaryCompareWnd.h"

#include "CompareWnd.h"

#include "Dialogs.h"
#include "Theme.h"
#include "resource.h"

namespace mf
{
    namespace
    {
        const int kHexColumn = 10;                              // after "00000000  "
        const int kAsciiColumn = kHexColumn + 16 * 3 + 2;       // after the hex bytes and a gap

        int HexColumnOf(int index)
        {
            return kHexColumn + index * 3 + (index >= 8 ? 1 : 0);
        }

        std::wstring LeafName(const std::wstring& path)
        {
            const size_t slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? path : path.substr(slash + 1);
        }
    }

    // ---- HexPane ----

    BEGIN_MESSAGE_MAP(HexPane, CWnd)
        ON_WM_PAINT()
        ON_WM_ERASEBKGND()
        ON_WM_SIZE()
        ON_WM_VSCROLL()
        ON_WM_MOUSEWHEEL()
        ON_WM_LBUTTONDOWN()
        ON_WM_KEYDOWN()
        ON_WM_GETDLGCODE()
    END_MESSAGE_MAP()

    BOOL HexPane::Create(CWnd* parent)
    {
        const CString className = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW));
        if (!CWnd::Create(className, L"Hex", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP, CRect(), parent, 700 + m_side))
            return FALSE;
        UpdateMetrics();
        return TRUE;
    }

    void HexPane::UpdateMetrics()
    {
        CClientDC dc(this);
        CFont* old = dc.SelectObject(&GetFonts().code);
        TEXTMETRICW tm = {};
        dc.GetTextMetrics(&tm);
        m_charWidth = (std::max)(1, static_cast<int>(dc.GetTextExtent(L"0", 1).cx));
        dc.SelectObject(old);
        m_rowHeight = tm.tmHeight + GetFonts().Scale(4);
        Refresh();
    }

    int HexPane::Rows() const
    {
        const size_t size = m_owner.Data(m_side).size();
        return (std::max)(1, static_cast<int>((size + kBytesPerRow - 1) / kBytesPerRow));
    }

    int HexPane::VisibleRows() const
    {
        CRect client;
        GetClientRect(&client);
        return (std::max)(1, client.Height() / m_rowHeight);
    }

    void HexPane::UpdateScrollBar()
    {
        if (!m_hWnd)
            return;
        const int visible = VisibleRows();
        SCROLLINFO info = { sizeof(info), SIF_ALL | SIF_DISABLENOSCROLL, 0, Rows() + visible / 2 - 1, static_cast<UINT>(visible), m_top, 0 };
        SetScrollInfo(SB_VERT, &info, TRUE);
    }

    void HexPane::Refresh()
    {
        m_top = (std::max)(0, (std::min)(m_top, Rows() - 1));
        UpdateScrollBar();
        Invalidate(FALSE);
    }

    void HexPane::SetTopRow(int row, bool sync)
    {
        row = (std::max)(0, (std::min)(row, Rows() - (std::max)(1, VisibleRows() / 2)));
        if (row == m_top)
            return;
        m_top = row;
        UpdateScrollBar();
        Invalidate(FALSE);
        if (sync)
            m_owner.SyncScroll(m_side);
    }

    void HexPane::SetCaret(int64_t offset)
    {
        const int64_t size = static_cast<int64_t>(m_owner.Data(m_side).size());
        m_caret = size == 0 ? -1 : (std::max)(static_cast<int64_t>(0), (std::min)(offset, size - 1));
        if (m_caret >= 0)
        {
            const int row = static_cast<int>(m_caret / kBytesPerRow);
            if (row < m_top)
                SetTopRow(row, true);
            else if (row >= m_top + VisibleRows())
                SetTopRow(row - VisibleRows() + 1, true);
        }
        Invalidate(FALSE);
    }

    BOOL HexPane::OnEraseBkgnd(CDC*)
    {
        return TRUE;
    }

    void HexPane::OnSize(UINT type, int cx, int cy)
    {
        CWnd::OnSize(type, cx, cy);
        UpdateScrollBar();
        Invalidate(FALSE);
    }

    void HexPane::OnPaint()
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
        dc.FillSolidRect(&client, theme.paneBack);
        dc.SetBkMode(TRANSPARENT);

        const std::string& data = m_owner.Data(m_side);
        const int left = GetFonts().Scale(8);
        if (data.empty())
        {
            dc.SetTextColor(theme.chromeLabel);
            dc.TextOutW(left, GetFonts().Scale(6), L"(empty file)");
        }
        const int rows = Rows();
        const COLORREF oneSided = m_side == 0 ? theme.removeChar : theme.insertChar;
        for (int row = m_top; row < (std::min)(rows, m_top + VisibleRows() + 1) && !data.empty(); ++row)
        {
            const int y = (row - m_top) * m_rowHeight;
            const int64_t first = static_cast<int64_t>(row) * kBytesPerRow;
            const int count = static_cast<int>((std::min)(static_cast<int64_t>(kBytesPerRow), static_cast<int64_t>(data.size()) - first));
            bool rowDiffers = false;
            for (int i = 0; i < count; ++i)
                rowDiffers = rowDiffers || m_owner.ByteState(m_side, first + i) != 0;
            if (rowDiffers)
                dc.FillSolidRect(0, y, client.Width(), m_rowHeight, theme.changeBack);

            CString offset;
            offset.Format(L"%08IX", static_cast<unsigned long long>(first));
            dc.SetTextColor(theme.lineNumber);
            dc.TextOutW(left, y + GetFonts().Scale(2), offset);

            for (int i = 0; i < count; ++i)
            {
                const int64_t at = first + i;
                const unsigned char byte = static_cast<unsigned char>(data[static_cast<size_t>(at)]);
                const uint8_t state = m_owner.ByteState(m_side, at);
                const int hexX = left + HexColumnOf(i) * m_charWidth;
                const int asciiX = left + (kAsciiColumn + i) * m_charWidth;
                if (state != 0)
                {
                    COLORREF color = state == 1 ? theme.changeChar : oneSided;
                    if (m_owner.InCurrentDifference(m_side, at))
                        color = Blend(color, RGB(0, 0, 0), 15);
                    dc.FillSolidRect(hexX - m_charWidth / 2, y + 1, m_charWidth * 3, m_rowHeight - 2, color);
                    dc.FillSolidRect(asciiX, y + 1, m_charWidth, m_rowHeight - 2, color);
                }
                if (at == m_caret)
                {
                    CBrush frame(theme.accent);
                    CRect hexRect(hexX - m_charWidth / 2, y, hexX + m_charWidth * 5 / 2, y + m_rowHeight);
                    CRect asciiRect(asciiX, y, asciiX + m_charWidth, y + m_rowHeight);
                    dc.FrameRect(&hexRect, &frame);
                    dc.FrameRect(&asciiRect, &frame);
                }
                wchar_t hex[3];
                swprintf_s(hex, L"%02X", byte);
                dc.SetTextColor(state != 0 ? theme.text : (byte == 0 ? theme.whitespace : theme.text));
                dc.TextOutW(hexX, y + GetFonts().Scale(2), hex, 2);
                const bool printable = byte >= 0x20 && byte < 0x7F;
                const wchar_t ch = printable ? static_cast<wchar_t>(byte) : L'.';
                dc.SetTextColor(printable ? theme.text : theme.whitespace);
                dc.TextOutW(asciiX, y + GetFonts().Scale(2), &ch, 1);
            }
        }
        // Separator between hex and characters.
        const int separator = left + (kAsciiColumn - 1) * m_charWidth;
        dc.FillSolidRect(separator, 0, 1, client.Height(), theme.chromeBorder);

        dc.SelectObject(oldFont);
        paint.BitBlt(0, 0, client.Width(), client.Height(), &dc, 0, 0, SRCCOPY);
        dc.SelectObject(oldBitmap);
    }

    void HexPane::OnVScroll(UINT code, UINT, CScrollBar*)
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
        default:
            return;
        }
        SetTopRow(top, true);
    }

    BOOL HexPane::OnMouseWheel(UINT, short delta, CPoint)
    {
        SetTopRow(m_top - delta * 3 / WHEEL_DELTA, true);
        return TRUE;
    }

    void HexPane::OnLButtonDown(UINT, CPoint point)
    {
        SetFocus();
        const int left = GetFonts().Scale(8);
        const int column = (point.x - left) / m_charWidth;
        int index = -1;
        if (column >= kAsciiColumn && column < kAsciiColumn + kBytesPerRow)
            index = column - kAsciiColumn;
        else
        {
            for (int i = 0; i < kBytesPerRow; ++i)
            {
                if (column >= HexColumnOf(i) - 1 && column <= HexColumnOf(i) + 1)
                    index = i;
            }
        }
        if (index < 0)
            return;
        SetCaret(static_cast<int64_t>(m_top + point.y / m_rowHeight) * kBytesPerRow + index);
        m_owner.OnCaretMoved(m_side);
    }

    void HexPane::OnKeyDown(UINT key, UINT repeat, UINT flags)
    {
        const int64_t caret = (std::max)(static_cast<int64_t>(0), m_caret);
        switch (key)
        {
        case VK_LEFT:
            SetCaret(caret - 1);
            break;
        case VK_RIGHT:
            SetCaret(caret + 1);
            break;
        case VK_UP:
            SetCaret(caret - kBytesPerRow);
            break;
        case VK_DOWN:
            SetCaret(caret + kBytesPerRow);
            break;
        case VK_PRIOR:
            SetCaret(caret - static_cast<int64_t>(VisibleRows()) * kBytesPerRow);
            break;
        case VK_NEXT:
            SetCaret(caret + static_cast<int64_t>(VisibleRows()) * kBytesPerRow);
            break;
        case VK_HOME:
            SetCaret(0);
            break;
        case VK_END:
            SetCaret(static_cast<int64_t>(m_owner.Data(m_side).size()) - 1);
            break;
        default:
            CWnd::OnKeyDown(key, repeat, flags);
            return;
        }
        m_owner.OnCaretMoved(m_side);
    }

    UINT HexPane::OnGetDlgCode()
    {
        return DLGC_WANTARROWS;
    }

    // ---- BinaryCompareWnd ----

    BEGIN_MESSAGE_MAP(BinaryCompareWnd, TabWindow)
        ON_WM_PAINT()
        ON_WM_ERASEBKGND()
        ON_WM_SIZE()
        ON_WM_SETFOCUS()
        ON_COMMAND(ID_MERGE_NEXT_CHANGE, &BinaryCompareWnd::OnNextChange)
        ON_COMMAND(ID_MERGE_PREV_CHANGE, &BinaryCompareWnd::OnPrevChange)
        ON_UPDATE_COMMAND_UI(ID_MERGE_NEXT_CHANGE, &BinaryCompareWnd::OnUpdateNextChange)
        ON_UPDATE_COMMAND_UI(ID_MERGE_PREV_CHANGE, &BinaryCompareWnd::OnUpdatePrevChange)
        ON_COMMAND(ID_VIEW_RECOMPARE, &BinaryCompareWnd::OnReload)
        ON_COMMAND(ID_VIEW_START, &BinaryCompareWnd::OnReload)
        ON_UPDATE_COMMAND_UI(ID_VIEW_RECOMPARE, &BinaryCompareWnd::OnUpdateAlways)
        ON_UPDATE_COMMAND_UI(ID_VIEW_START, &BinaryCompareWnd::OnUpdateAlways)
        ON_COMMAND(ID_VIEW_STATISTICS, &BinaryCompareWnd::OnStatistics)
        ON_UPDATE_COMMAND_UI(ID_VIEW_STATISTICS, &BinaryCompareWnd::OnUpdateAlways)
    END_MESSAGE_MAP()

    BinaryCompareWnd::BinaryCompareWnd() = default;
    BinaryCompareWnd::~BinaryCompareWnd() = default;

    BOOL BinaryCompareWnd::Create(CWnd* parent, UINT id)
    {
        const CString className = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW));
        if (!CWnd::Create(className, L"Binary comparison", WS_CHILD | WS_CLIPCHILDREN, CRect(), parent, id))
            return FALSE;
        for (int side = 0; side < 2; ++side)
        {
            m_panes[side] = std::make_unique<HexPane>(*this, side);
            m_panes[side]->Create(this);
        }
        return TRUE;
    }

    bool BinaryCompareWnd::Open(const std::wstring& left, const std::wstring& right, CString& error)
    {
        m_paths[0] = left;
        m_paths[1] = right;
        for (int side = 0; side < 2; ++side)
        {
            std::wstring message;
            if (!ReadFileBytes(m_paths[side], m_data[side], message))
            {
                error.Format(L"Cannot open %s:\n%s", m_paths[side].c_str(), message.c_str());
                return false;
            }
        }
        Compare();
        return true;
    }

    void BinaryCompareWnd::Compare()
    {
        m_diff = DiffBytes(m_data[0], m_data[1]);
        for (int side = 0; side < 2; ++side)
        {
            m_state[side].assign(m_data[side].size(), 0);
            for (const Chunk& chunk : m_diff.chunks)
            {
                if (!chunk.IsDifference())
                    continue;
                const uint8_t state = chunk.count[1 - side] == 0 ? 2 : 1;
                std::fill(m_state[side].begin() + chunk.start[side], m_state[side].begin() + chunk.End(side), state);
            }
        }
        m_current = -1;
        for (auto& pane : m_panes)
        {
            if (pane)
                pane->Refresh();
        }
        NotifyFrame();
    }

    void BinaryCompareWnd::OnShown()
    {
        Layout();
        const int first = FindDifference(true);
        if (first >= 0)
            GoToDifference(first);
        m_panes[0]->SetFocus();
    }

    uint8_t BinaryCompareWnd::ByteState(int side, int64_t offset) const
    {
        return offset >= 0 && offset < static_cast<int64_t>(m_state[side].size()) ? m_state[side][static_cast<size_t>(offset)] : 0;
    }

    bool BinaryCompareWnd::InCurrentDifference(int side, int64_t offset) const
    {
        if (m_current < 0)
            return false;
        const Chunk& chunk = m_diff.chunks[static_cast<size_t>(m_current)];
        return offset >= chunk.start[side] && offset < chunk.End(side);
    }

    double BinaryCompareWnd::MapOffset(int from, double offset, int to) const
    {
        const std::vector<Chunk>& chunks = m_diff.chunks;
        if (chunks.empty())
            return offset;
        auto it = std::upper_bound(chunks.begin(), chunks.end(), offset, [from](double value, const Chunk& chunk) { return value < chunk.End(from); });
        if (it == chunks.end())
            return offset - chunks.back().End(from) + chunks.back().End(to);
        if (it->count[from] == 0)
            return it->start[to];
        return it->start[to] + (offset - it->start[from]) / it->count[from] * it->count[to];
    }

    void BinaryCompareWnd::SyncScroll(int fromSide)
    {
        if (m_syncing)
            return;
        m_syncing = true;
        HexPane* from = m_panes[fromSide].get();
        HexPane* to = m_panes[1 - fromSide].get();
        const double center = (from->TopRow() + from->VisibleRows() / 2.0) * HexPane::kBytesPerRow;
        const double mapped = MapOffset(fromSide, center, 1 - fromSide);
        to->SetTopRow(static_cast<int>(mapped / HexPane::kBytesPerRow - to->VisibleRows() / 2.0 + 0.5), false);
        m_syncing = false;
    }

    void BinaryCompareWnd::OnCaretMoved(int side)
    {
        m_activeSide = side;
        const int64_t caret = m_panes[side]->Caret();
        if (caret >= 0)
        {
            // The corresponding byte on the other side.
            m_syncing = true;
            m_panes[1 - side]->SetCaret(static_cast<int64_t>(MapOffset(side, static_cast<double>(caret), 1 - side)));
            m_syncing = false;
            for (size_t k = 0; k < m_diff.chunks.size(); ++k)
            {
                const Chunk& chunk = m_diff.chunks[k];
                if (caret >= chunk.start[side] && caret < chunk.End(side))
                {
                    m_current = chunk.IsDifference() ? static_cast<int>(k) : -1;
                    break;
                }
            }
        }
        m_panes[0]->Invalidate(FALSE);
        m_panes[1]->Invalidate(FALSE);
        NotifyFrame();
    }

    int BinaryCompareWnd::FindDifference(bool forward) const
    {
        const int count = static_cast<int>(m_diff.chunks.size());
        int from = m_current;
        if (from < 0)
        {
            // From the top of the active pane.
            const int64_t offset = static_cast<int64_t>(m_panes[m_activeSide] ? m_panes[m_activeSide]->TopRow() : 0) * HexPane::kBytesPerRow;
            from = forward ? -1 : count;
            for (int k = 0; k < count; ++k)
            {
                if (m_diff.chunks[static_cast<size_t>(k)].End(m_activeSide) > offset)
                {
                    from = forward ? k - 1 : k;
                    break;
                }
            }
        }
        for (int k = forward ? from + 1 : from - 1; k >= 0 && k < count; k += forward ? 1 : -1)
        {
            if (m_diff.chunks[static_cast<size_t>(k)].IsDifference())
                return k;
        }
        return -1;
    }

    void BinaryCompareWnd::GoToDifference(int index)
    {
        if (index < 0)
            return;
        m_current = index;
        const Chunk& chunk = m_diff.chunks[static_cast<size_t>(index)];
        for (int side = 0; side < 2; ++side)
        {
            HexPane* pane = m_panes[side].get();
            pane->SetTopRow(chunk.start[side] / HexPane::kBytesPerRow - pane->VisibleRows() / 3, false);
        }
        m_panes[0]->Invalidate(FALSE);
        m_panes[1]->Invalidate(FALSE);
        NotifyFrame();
    }

    void BinaryCompareWnd::OnNextChange()
    {
        GoToDifference(FindDifference(true));
    }

    void BinaryCompareWnd::OnPrevChange()
    {
        GoToDifference(FindDifference(false));
    }

    void BinaryCompareWnd::OnUpdateNextChange(CCmdUI* ui)
    {
        ui->Enable(FindDifference(true) >= 0);
    }

    void BinaryCompareWnd::OnUpdatePrevChange(CCmdUI* ui)
    {
        ui->Enable(FindDifference(false) >= 0);
    }

    void BinaryCompareWnd::OnUpdateAlways(CCmdUI* ui)
    {
        ui->Enable(TRUE);
    }

    void BinaryCompareWnd::OnReload()
    {
        CString error;
        if (!Open(m_paths[0], m_paths[1], error))
            AfxMessageBox(error, MB_ICONERROR);
        Invalidate(FALSE);
    }

    void BinaryCompareWnd::OnStatistics()
    {
        CString text;
        text.Format(L"Left: %s\r\n    %zu bytes\r\n\r\nRight: %s\r\n    %zu bytes\r\n\r\n%d difference(s), %I64u byte(s) differ.\r\n%s", m_paths[0].c_str(), m_data[0].size(),
            m_paths[1].c_str(), m_data[1].size(), m_diff.Differences(), static_cast<unsigned long long>(m_diff.differentBytes),
            m_diff.aligned ? L"Compared offset by offset (same size, or too large for a byte-level diff)." : L"Inserted and removed bytes were found with a byte-level diff.");
        TextDlg dialog(IDD_STATISTICS, text, this);
        dialog.DoModal();
    }

    TabItem BinaryCompareWnd::GetTabItem() const
    {
        TabItem item;
        item.kind = TabKind::Binary;
        item.title = (LeafName(m_paths[0]) + L", " + LeafName(m_paths[1])).c_str();
        item.tooltip = (m_paths[0] + L"\n" + m_paths[1]).c_str();
        return item;
    }

    CString BinaryCompareWnd::StatusText(int part) const
    {
        CString text;
        if (part == 0)
            text = m_diff.aligned ? L"Binary comparison (offset by offset)" : L"Binary comparison (insertions found)";
        else if (part == 1)
            text.Format(m_diff.Differences() ? L"%d differences \x2022 %I64u bytes differ" : L"No differences", m_diff.Differences(), static_cast<unsigned long long>(m_diff.differentBytes));
        else if (m_panes[m_activeSide] && m_panes[m_activeSide]->Caret() >= 0)
        {
            const int64_t caret = m_panes[m_activeSide]->Caret();
            text.Format(L"Offset 0x%I64X (%I64d) \x2022 byte 0x%02X of %zu", caret, caret, static_cast<unsigned char>(m_data[m_activeSide][static_cast<size_t>(caret)]),
                m_data[m_activeSide].size());
        }
        else
            text.Format(L"%zu and %zu bytes", m_data[0].size(), m_data[1].size());
        return text;
    }

    void BinaryCompareWnd::NotifyFrame()
    {
        if (CFrameWnd* frame = GetParentFrame())
            frame->PostMessage(WM_MF_STATECHANGED);
    }

    void BinaryCompareWnd::UpdateMetrics()
    {
        for (auto& pane : m_panes)
            pane->UpdateMetrics();
        Layout();
    }

    void BinaryCompareWnd::Layout()
    {
        if (!m_hWnd || !m_panes[0])
            return;
        CRect client;
        GetClientRect(&client);
        const int top = GetFonts().Scale(30);
        const int gap = GetFonts().Scale(12);
        const int width = (client.Width() - gap) / 2;
        m_panes[0]->MoveWindow(0, top, width, (std::max)(0, static_cast<int>(client.Height()) - top));
        m_panes[1]->MoveWindow(width + gap, top, client.Width() - width - gap, (std::max)(0, static_cast<int>(client.Height()) - top));
        Invalidate(FALSE);
    }

    void BinaryCompareWnd::OnSize(UINT type, int cx, int cy)
    {
        TabWindow::OnSize(type, cx, cy);
        Layout();
    }

    void BinaryCompareWnd::OnSetFocus(CWnd*)
    {
        if (m_panes[m_activeSide])
            m_panes[m_activeSide]->SetFocus();
    }

    BOOL BinaryCompareWnd::OnEraseBkgnd(CDC*)
    {
        return TRUE;
    }

    void BinaryCompareWnd::OnPaint()
    {
        CPaintDC dc(this);
        CRect client;
        GetClientRect(&client);
        const Theme& theme = GetTheme();
        Fonts& fonts = GetFonts();
        dc.FillSolidRect(&client, theme.chromeBack);
        dc.SetBkMode(TRANSPARENT);
        const int gap = fonts.Scale(12);
        const int width = (client.Width() - gap) / 2;
        for (int side = 0; side < 2; ++side)
        {
            CRect bar(side == 0 ? 0 : width + gap, fonts.Scale(3), side == 0 ? width : client.right, fonts.Scale(27));
            CBrush back(theme.pathBarBack);
            CPen border(PS_SOLID, 1, theme.chromeBorder);
            CBrush* ob = dc.SelectObject(&back);
            CPen* op = dc.SelectObject(&border);
            dc.RoundRect(&bar, CPoint(fonts.Scale(6), fonts.Scale(6)));
            dc.SelectObject(ob);
            dc.SelectObject(op);
            CRect text = bar;
            text.DeflateRect(fonts.Scale(8), 0);
            CFont* oldFont = dc.SelectObject(&fonts.uiSmall);
            CString size;
            size.Format(L"%zu BYTES", m_data[side].size());
            dc.SetTextColor(theme.chromeLabel);
            dc.DrawText(size, &text, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            text.right -= dc.GetTextExtent(size).cx + fonts.Scale(10);
            dc.SelectObject(&fonts.ui);
            dc.SetTextColor(theme.text);
            dc.DrawText(m_paths[side].c_str(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
            dc.SelectObject(oldFont);
        }
    }
}
