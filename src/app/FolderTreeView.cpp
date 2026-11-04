// FolderTreeView.cpp - drawing and navigating the folder comparison tree.
#include "FolderTreeView.h"

#include "FolderCompareWnd.h"
#include "Theme.h"
#include "resource.h"

#include <cwctype>

namespace mf
{
    namespace
    {
        const wchar_t kChevronRight = 0xE76C;
        const wchar_t kChevronDown = 0xE70D;
        const wchar_t kFolderGlyph = 0xE8B7;
        const wchar_t kFileGlyph = 0xE8A5;

        COLORREF StatusBack(ItemStatus status)
        {
            const Theme& theme = GetTheme();
            switch (status)
            {
            case ItemStatus::LeftOnly:
            case ItemStatus::LeftChanged:
                return theme.removeBack;
            case ItemStatus::RightOnly:
            case ItemStatus::RightChanged:
                return theme.insertBack;
            case ItemStatus::Different:
            case ItemStatus::BothSame:
                return theme.changeBack;
            case ItemStatus::Conflict:
            case ItemStatus::Error:
                return theme.conflictBack;
            default:
                return theme.paneBack;
            }
        }

        CString FormatSize(uint64_t size)
        {
            CString digits;
            digits.Format(L"%I64u", size);
            CString grouped;
            const int length = digits.GetLength();
            for (int i = 0; i < length; ++i)
            {
                grouped += digits[i];
                if ((length - i - 1) % 3 == 0 && i + 1 < length)
                    grouped += L',';
            }
            return grouped;
        }

        CString FormatTime(int64_t ticks)
        {
            if (ticks <= 0)
                return L"";  // unknown (e.g. a folder inside an archive)
            FILETIME utc = { static_cast<DWORD>(ticks & 0xFFFFFFFF), static_cast<DWORD>(static_cast<uint64_t>(ticks) >> 32) };
            FILETIME local = {};
            SYSTEMTIME time = {};
            if (!FileTimeToLocalFileTime(&utc, &local) || !FileTimeToSystemTime(&local, &time))
                return L"";
            CString text;
            text.Format(L"%04d-%02d-%02d %02d:%02d", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
            return text;
        }
    }

    BEGIN_MESSAGE_MAP(FolderTreeView, CWnd)
        ON_WM_PAINT()
        ON_WM_ERASEBKGND()
        ON_WM_SIZE()
        ON_WM_VSCROLL()
        ON_WM_MOUSEWHEEL()
        ON_WM_LBUTTONDOWN()
        ON_WM_LBUTTONDBLCLK()
        ON_WM_RBUTTONDOWN()
        ON_WM_CONTEXTMENU()
        ON_WM_KEYDOWN()
        ON_WM_SETFOCUS()
        ON_WM_KILLFOCUS()
        ON_WM_GETDLGCODE()
    END_MESSAGE_MAP()

    BOOL FolderTreeView::Create(CWnd* parent)
    {
        const CString className = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW));
        if (!CWnd::Create(className, L"Folders", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP, CRect(), parent, 500))
            return FALSE;
        UpdateMetrics();
        return TRUE;
    }

    void FolderTreeView::UpdateMetrics()
    {
        CClientDC dc(this);
        CFont* old = dc.SelectObject(&GetFonts().ui);
        TEXTMETRICW tm = {};
        dc.GetTextMetrics(&tm);
        dc.SelectObject(old);
        m_rowHeight = tm.tmHeight + GetFonts().Scale(8);
        m_headerHeight = tm.tmHeight + GetFonts().Scale(10);
        UpdateScrollBar();
        Invalidate(FALSE);
    }

    const FolderComparison* FolderTreeView::Comparison() const
    {
        return m_owner.Comparison();
    }

    std::wstring FolderTreeView::Key(const FolderItem& item)
    {
        std::wstring key = item.relPath;
        for (wchar_t& c : key)
            c = static_cast<wchar_t>(std::towlower(c));
        return key;
    }

    bool FolderTreeView::IsExpanded(int item) const
    {
        return m_expanded.count(Key(Comparison()->Item(item))) != 0;
    }

    void FolderTreeView::SetExpanded(int item, bool expanded)
    {
        const std::wstring key = Key(Comparison()->Item(item));
        if (expanded)
            m_expanded.insert(key);
        else
            m_expanded.erase(key);
        Rebuild();
    }

    bool FolderTreeView::HasVisibleContent(int item) const
    {
        const FolderItem& entry = Comparison()->Item(item);
        if (!entry.isDir)
            return m_owner.IsItemVisible(entry);
        // A folder shows when its own status is shown (it exists on one side only) or something inside it shows.
        const bool oneSided = entry.status != ItemStatus::Identical && entry.status != ItemStatus::Different && entry.status != ItemStatus::Conflict;
        if (oneSided && m_owner.IsItemVisible(entry))
            return true;
        if (entry.children.empty())
            return m_owner.IsItemVisible(entry);
        for (int child : entry.children)
        {
            if (HasVisibleContent(child))
                return true;
        }
        return false;
    }

    void FolderTreeView::AddRows(int item, int depth)
    {
        if (!HasVisibleContent(item))
            return;
        m_rows.push_back({ item, depth });
        const FolderItem& entry = Comparison()->Item(item);
        if (entry.isDir && IsExpanded(item))
        {
            for (int child : entry.children)
                AddRows(child, depth + 1);
        }
    }

    void FolderTreeView::Rebuild()
    {
        m_rows.clear();
        const FolderComparison* comparison = Comparison();
        if (comparison && !comparison->Items().empty())
        {
            if (!m_initialized)
            {
                // Open every folder that has differences inside.
                m_initialized = true;
                for (const FolderItem& item : comparison->Items())
                {
                    if (item.isDir && item.parent >= 0 && item.status != ItemStatus::Identical && item.status != ItemStatus::Pending &&
                        item.status != ItemStatus::LeftOnly && item.status != ItemStatus::RightOnly)
                        m_expanded.insert(Key(item));
                }
            }
            for (int child : comparison->Item(0).children)
                AddRows(child, 0);
        }
        // Restore the focus and drop selected keys that are gone.
        m_focus = -1;
        std::set<std::wstring> present;
        for (size_t row = 0; row < m_rows.size(); ++row)
        {
            const std::wstring key = Key(comparison->Item(m_rows[row].item));
            present.insert(key);
            if (key == m_focusKey)
                m_focus = static_cast<int>(row);
        }
        for (auto it = m_selected.begin(); it != m_selected.end();)
            it = present.count(*it) ? std::next(it) : m_selected.erase(it);
        if (m_focus < 0 && !m_rows.empty())
            m_focus = 0;
        m_anchor = m_focus;
        m_top = (std::max)(0, (std::min)(m_top, static_cast<int>(m_rows.size()) - VisibleRows()));
        UpdateScrollBar();
        Invalidate(FALSE);
    }

    void FolderTreeView::ExpandAll(bool expand)
    {
        m_expanded.clear();
        if (expand && Comparison())
        {
            for (const FolderItem& item : Comparison()->Items())
            {
                if (item.isDir && item.parent >= 0)
                    m_expanded.insert(Key(item));
            }
        }
        Rebuild();
    }

    std::vector<int> FolderTreeView::SelectedItems() const
    {
        std::vector<int> items;
        for (const Row& row : m_rows)
        {
            if (m_selected.count(Key(Comparison()->Item(row.item))))
                items.push_back(row.item);
        }
        return items;
    }

    int FolderTreeView::FocusedItem() const
    {
        return m_focus >= 0 && m_focus < static_cast<int>(m_rows.size()) ? m_rows[static_cast<size_t>(m_focus)].item : -1;
    }

    int FolderTreeView::VisibleRows() const
    {
        CRect client;
        GetClientRect(&client);
        return (std::max)(1, (client.Height() - m_headerHeight) / m_rowHeight);
    }

    void FolderTreeView::UpdateScrollBar()
    {
        if (!m_hWnd)
            return;
        const int visible = VisibleRows();
        SCROLLINFO info = { sizeof(info), SIF_ALL | SIF_DISABLENOSCROLL, 0, (std::max)(0, static_cast<int>(m_rows.size()) - 1), static_cast<UINT>(visible), m_top, 0 };
        SetScrollInfo(SB_VERT, &info, TRUE);
    }

    void FolderTreeView::EnsureVisible(int row)
    {
        const int visible = VisibleRows();
        if (row < m_top)
            m_top = row;
        else if (row >= m_top + visible)
            m_top = row - visible + 1;
        m_top = (std::max)(0, m_top);
        UpdateScrollBar();
        Invalidate(FALSE);
    }

    void FolderTreeView::SetFocusRow(int row, bool extend, bool toggle)
    {
        if (m_rows.empty())
            return;
        row = (std::max)(0, (std::min)(row, static_cast<int>(m_rows.size()) - 1));
        auto keyOf = [this](int r) { return Key(Comparison()->Item(m_rows[static_cast<size_t>(r)].item)); };
        if (extend && m_anchor >= 0)
        {
            m_selected.clear();
            for (int r = (std::min)(m_anchor, row); r <= (std::max)(m_anchor, row); ++r)
                m_selected.insert(keyOf(r));
        }
        else if (toggle)
        {
            const std::wstring key = keyOf(row);
            if (!m_selected.erase(key))
                m_selected.insert(key);
            m_anchor = row;
        }
        else
        {
            m_selected = { keyOf(row) };
            m_anchor = row;
        }
        m_focus = row;
        m_focusKey = keyOf(row);
        EnsureVisible(row);
        m_owner.OnSelectionChanged();
    }

    int FolderTreeView::RowAt(CPoint point) const
    {
        if (point.y < m_headerHeight)
            return -1;
        const int row = m_top + (point.y - m_headerHeight) / m_rowHeight;
        return row < static_cast<int>(m_rows.size()) ? row : -1;
    }

    void FolderTreeView::Columns(std::vector<int>& x) const
    {
        CRect client;
        GetClientRect(&client);
        const Fonts& fonts = GetFonts();
        const int sides = Comparison() ? Comparison()->SideCount() : 2;
        const int status = fonts.Scale(140);
        const int size = fonts.Scale(84);
        const int time = fonts.Scale(128);
        const int fixed = status + sides * (size + time);
        const int name = (std::max)(fonts.Scale(220), static_cast<int>(client.Width()) - fixed);
        x.clear();
        x.push_back(0);
        x.push_back(name);
        x.push_back(name + status);
        for (int side = 0; side < sides; ++side)
        {
            x.push_back(x.back() + size);
            x.push_back(x.back() + time);
        }
    }

    BOOL FolderTreeView::OnEraseBkgnd(CDC*)
    {
        return TRUE;
    }

    void FolderTreeView::OnSize(UINT type, int cx, int cy)
    {
        CWnd::OnSize(type, cx, cy);
        UpdateScrollBar();
        Invalidate(FALSE);
    }

    void FolderTreeView::OnPaint()
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
        dc.FillSolidRect(&client, theme.paneBack);
        dc.SetBkMode(TRANSPARENT);
        CFont* oldFont = dc.SelectObject(&fonts.ui);
        std::vector<int> x;
        Columns(x);
        const FolderComparison* comparison = Comparison();
        const int sides = comparison ? comparison->SideCount() : 2;
        const int pad = fonts.Scale(8);

        // Header
        dc.FillSolidRect(0, 0, client.Width(), m_headerHeight, theme.pathBarBack);
        dc.FillSolidRect(0, m_headerHeight - 1, client.Width(), 1, theme.chromeBorder);
        dc.SelectObject(&fonts.uiBold);
        dc.SetTextColor(theme.chromeText);
        static const wchar_t* twoWay[] = { L"Left", L"Right" };
        static const wchar_t* threeWay[] = { L"Left", L"Ancestor", L"Right" };
        auto header = [&](int column, const CString& text, UINT align) {
            CRect r(x[static_cast<size_t>(column)] + pad, 0, x[static_cast<size_t>(column) + 1] - pad, m_headerHeight);
            dc.DrawText(text, &r, align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            if (column > 0)
                dc.FillSolidRect(x[static_cast<size_t>(column)], fonts.Scale(4), 1, m_headerHeight - fonts.Scale(8), theme.chromeBorder);
        };
        header(0, L"Name", DT_LEFT);
        header(1, L"Status", DT_LEFT);
        for (int side = 0; side < sides; ++side)
        {
            const CString name = sides == 3 ? threeWay[side] : twoWay[side];
            header(2 + side * 2, name + L" size", DT_RIGHT);
            header(3 + side * 2, name + L" modified", DT_LEFT);
        }

        dc.SelectObject(&fonts.ui);
        if (!comparison || m_rows.empty())
        {
            dc.SetTextColor(theme.chromeLabel);
            CRect r(0, m_headerHeight + fonts.Scale(20), client.Width(), m_headerHeight + fonts.Scale(60));
            dc.DrawText(comparison ? L"Nothing to show with the current filters." : L"Comparing\x2026", &r, DT_CENTER | DT_TOP | DT_SINGLELINE);
        }
        const int visible = VisibleRows() + 1;
        for (int row = m_top; row < (std::min)(static_cast<int>(m_rows.size()), m_top + visible); ++row)
        {
            const Row& info = m_rows[static_cast<size_t>(row)];
            const FolderItem& item = comparison->Item(info.item);
            const int y = m_headerHeight + (row - m_top) * m_rowHeight;
            const bool selected = m_selected.count(Key(item)) != 0;
            COLORREF back = StatusBack(item.status);
            if (item.isDir && back != theme.paneBack)
                back = Blend(back, theme.paneBack, 45);
            if (selected)
                back = m_hasFocus ? theme.selection : theme.selectionInactive;
            dc.FillSolidRect(0, y, client.Width(), m_rowHeight, back);
            if (row == m_focus && m_hasFocus)
            {
                CBrush frame(theme.accent);
                CRect r(0, y, client.Width(), y + m_rowHeight);
                dc.FrameRect(&r, &frame);
            }

            // Name: indent, chevron, glyph, text.
            const int indent = pad + info.depth * fonts.Scale(18);
            if (item.isDir && !item.children.empty())
                DrawGlyph(dc, CRect(indent, y, indent + fonts.Scale(14), y + m_rowHeight), IsExpanded(info.item) ? kChevronDown : kChevronRight, theme.chromeText, fonts.iconsSmall);
            const int glyphX = indent + fonts.Scale(16);
            DrawGlyph(dc, CRect(glyphX, y, glyphX + fonts.Scale(18), y + m_rowHeight), item.isDir ? kFolderGlyph : kFileGlyph,
                item.isDir ? RGB(200, 150, 40) : theme.chromeLabel, fonts.iconsSmall);
            dc.SetTextColor(theme.text);
            CRect nameRect(glyphX + fonts.Scale(22), y, x[1] - pad, y + m_rowHeight);
            dc.DrawText(item.name.c_str(), -1, &nameRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            CRect statusRect(x[1] + pad, y, x[2] - pad, y + m_rowHeight);
            dc.SetTextColor(item.status == ItemStatus::Identical || item.status == ItemStatus::Pending ? theme.chromeLabel : theme.text);
            CString status = StatusName(item.status);
            if (item.isDir && item.status == ItemStatus::Different)
                status = L"Contains differences";
            dc.DrawText(status, &statusRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            for (int side = 0; side < sides; ++side)
            {
                const FolderSide& info2 = item.side[side];
                CRect sizeRect(x[static_cast<size_t>(2 + side * 2)] + pad, y, x[static_cast<size_t>(3 + side * 2)] - pad, y + m_rowHeight);
                CRect timeRect(x[static_cast<size_t>(3 + side * 2)] + pad, y, x[static_cast<size_t>(4 + side * 2)] - pad, y + m_rowHeight);
                if (!info2.exists)
                {
                    dc.SetTextColor(theme.disabled);
                    dc.DrawText(L"\x2014", 1, &sizeRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
                    continue;
                }
                dc.SetTextColor(theme.chromeText);
                if (!info2.isDir)
                    dc.DrawText(FormatSize(info2.size), &sizeRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                dc.DrawText(FormatTime(info2.modified), &timeRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
        }

        dc.SelectObject(oldFont);
        paint.BitBlt(0, 0, client.Width(), client.Height(), &dc, 0, 0, SRCCOPY);
        dc.SelectObject(oldBitmap);
    }

    void FolderTreeView::OnVScroll(UINT code, UINT, CScrollBar*)
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
            top -= VisibleRows();
            break;
        case SB_PAGEDOWN:
            top += VisibleRows();
            break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
            top = info.nTrackPos;
            break;
        default:
            return;
        }
        m_top = (std::max)(0, (std::min)(top, static_cast<int>(m_rows.size()) - VisibleRows()));
        UpdateScrollBar();
        Invalidate(FALSE);
    }

    BOOL FolderTreeView::OnMouseWheel(UINT, short delta, CPoint)
    {
        m_top = (std::max)(0, (std::min)(m_top - delta * 3 / WHEEL_DELTA, static_cast<int>(m_rows.size()) - VisibleRows()));
        UpdateScrollBar();
        Invalidate(FALSE);
        return TRUE;
    }

    void FolderTreeView::OnLButtonDown(UINT flags, CPoint point)
    {
        SetFocus();
        const int row = RowAt(point);
        if (row < 0)
            return;
        const Row& info = m_rows[static_cast<size_t>(row)];
        const FolderItem& item = Comparison()->Item(info.item);
        const int chevron = GetFonts().Scale(8) + info.depth * GetFonts().Scale(18);
        if (item.isDir && point.x >= chevron && point.x < chevron + GetFonts().Scale(16))
        {
            m_focusKey = Key(item);
            SetExpanded(info.item, !IsExpanded(info.item));
            return;
        }
        SetFocusRow(row, (flags & MK_SHIFT) != 0, (flags & MK_CONTROL) != 0);
    }

    void FolderTreeView::OnLButtonDblClk(UINT, CPoint point)
    {
        const int row = RowAt(point);
        if (row < 0)
            return;
        const int item = m_rows[static_cast<size_t>(row)].item;
        if (Comparison()->Item(item).isDir)
            SetExpanded(item, !IsExpanded(item));
        else
            m_owner.OpenItem(item, false);
    }

    void FolderTreeView::OnRButtonDown(UINT, CPoint point)
    {
        SetFocus();
        const int row = RowAt(point);
        if (row >= 0 && !m_selected.count(Key(Comparison()->Item(m_rows[static_cast<size_t>(row)].item))))
            SetFocusRow(row, false, false);
    }

    void FolderTreeView::OnContextMenu(CWnd*, CPoint point)
    {
        if (point.x == -1 && point.y == -1)
        {
            point = CPoint(GetFonts().Scale(40), m_headerHeight + ((std::max)(0, m_focus - m_top) + 1) * m_rowHeight);
            ClientToScreen(&point);
        }
        m_owner.ShowItemMenu(point);
    }

    void FolderTreeView::OnKeyDown(UINT key, UINT repeat, UINT flags)
    {
        const bool shift = GetKeyState(VK_SHIFT) < 0;
        const bool ctrl = GetKeyState(VK_CONTROL) < 0;
        const int item = FocusedItem();
        switch (key)
        {
        case VK_UP:
            SetFocusRow(m_focus - 1, shift, false);
            return;
        case VK_DOWN:
            SetFocusRow(m_focus + 1, shift, false);
            return;
        case VK_PRIOR:
            SetFocusRow(m_focus - VisibleRows(), shift, false);
            return;
        case VK_NEXT:
            SetFocusRow(m_focus + VisibleRows(), shift, false);
            return;
        case VK_HOME:
            SetFocusRow(0, shift, false);
            return;
        case VK_END:
            SetFocusRow(static_cast<int>(m_rows.size()) - 1, shift, false);
            return;
        case VK_LEFT:
            if (item >= 0 && Comparison()->Item(item).isDir && IsExpanded(item))
            {
                SetExpanded(item, false);
            }
            else if (item >= 0)
            {
                // Go to the parent folder's row.
                const int parent = Comparison()->Item(item).parent;
                for (int row = m_focus - 1; row >= 0; --row)
                {
                    if (m_rows[static_cast<size_t>(row)].item == parent)
                    {
                        SetFocusRow(row, false, false);
                        break;
                    }
                }
            }
            return;
        case VK_RIGHT:
            if (item >= 0 && Comparison()->Item(item).isDir)
            {
                if (!IsExpanded(item))
                    SetExpanded(item, true);
                else
                    SetFocusRow(m_focus + 1, false, false);
            }
            return;
        case VK_RETURN:
            if (item >= 0 && !Comparison()->Item(item).isDir)
                m_owner.OpenItem(item, shift);
            else if (item >= 0)
                SetExpanded(item, !IsExpanded(item));
            return;
        case VK_SPACE:
            if (ctrl)
                SetFocusRow(m_focus, false, true);
            return;
        case VK_ESCAPE:
            // Stops a running comparison.
            GetParentFrame()->SendMessage(WM_COMMAND, ID_VIEW_STOP);
            return;
        default:
            break;
        }
        CWnd::OnKeyDown(key, repeat, flags);
    }

    void FolderTreeView::OnSetFocus(CWnd* oldWnd)
    {
        CWnd::OnSetFocus(oldWnd);
        m_hasFocus = true;
        Invalidate(FALSE);
    }

    void FolderTreeView::OnKillFocus(CWnd* newWnd)
    {
        CWnd::OnKillFocus(newWnd);
        m_hasFocus = false;
        Invalidate(FALSE);
    }

    UINT FolderTreeView::OnGetDlgCode()
    {
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    }

    int FolderTreeView::SelectWhere(const std::function<bool(const FolderItem&)>& match)
    {
        m_selected.clear();
        int count = 0;
        for (const Row& row : m_rows)
        {
            const FolderItem& item = Comparison()->Item(row.item);
            if (!item.isDir && match(item))
            {
                m_selected.insert(Key(item));
                ++count;
            }
        }
        Invalidate(FALSE);
        m_owner.OnSelectionChanged();
        return count;
    }

    namespace
    {
        // Every item in tree order (pre-order).
        void TreeOrder(const FolderComparison& comparison, int item, std::vector<int>& order)
        {
            order.push_back(item);
            for (int child : comparison.Item(item).children)
                TreeOrder(comparison, child, order);
        }

        bool IsDifference(const FolderItem& item, bool conflictsOnly)
        {
            if (item.isDir && !item.children.empty())
                return false;
            return conflictsOnly ? item.status == ItemStatus::Conflict : item.status != ItemStatus::Identical && item.status != ItemStatus::Pending;
        }

        int FindDifference(const FolderComparison& comparison, int from, bool forward, bool conflictsOnly)
        {
            std::vector<int> order;
            TreeOrder(comparison, 0, order);
            auto position = std::find(order.begin(), order.end(), from);
            int index = position == order.end() ? (forward ? 0 : static_cast<int>(order.size())) : static_cast<int>(position - order.begin());
            for (int i = forward ? index + 1 : index - 1; i >= 0 && i < static_cast<int>(order.size()); i += forward ? 1 : -1)
            {
                if (order[static_cast<size_t>(i)] != 0 && IsDifference(comparison.Item(order[static_cast<size_t>(i)]), conflictsOnly))
                    return order[static_cast<size_t>(i)];
            }
            return -1;
        }
    }

    bool FolderTreeView::CanGoToDifference(bool forward, bool conflictsOnly) const
    {
        return Comparison() && FindDifference(*Comparison(), (std::max)(0, FocusedItem()), forward, conflictsOnly) >= 0;
    }

    bool FolderTreeView::GoToDifference(bool forward, bool conflictsOnly)
    {
        const FolderComparison* comparison = Comparison();
        if (!comparison)
            return false;
        const int target = FindDifference(*comparison, (std::max)(0, FocusedItem()), forward, conflictsOnly);
        if (target < 0)
            return false;
        for (int parent = comparison->Item(target).parent; parent > 0; parent = comparison->Item(parent).parent)
            m_expanded.insert(Key(comparison->Item(parent)));
        m_focusKey = Key(comparison->Item(target));
        Rebuild();
        if (m_focus >= 0 && m_rows[static_cast<size_t>(m_focus)].item == target)
            SetFocusRow(m_focus, false, false);
        return true;
    }
}
