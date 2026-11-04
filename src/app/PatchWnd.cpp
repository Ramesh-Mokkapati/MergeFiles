// PatchWnd.cpp - reviewing and selectively applying a patch file.
#include "PatchWnd.h"

#include "CompareWnd.h"
#include "MainFrame.h"
#include "Theme.h"
#include "resource.h"

namespace mf
{
    namespace
    {
        enum ControlId : UINT
        {
            kFolder = 900,
            kBrowse,
            kStrip,
            kReverse,
            kApply,
            kAll,
            kNone,
            kList,
            kText,
        };

        std::wstring ParentFolder(const std::wstring& path)
        {
            const size_t slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
        }

        std::wstring LeafName(const std::wstring& path)
        {
            const size_t slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? path : path.substr(slash + 1);
        }

        CString HunkHeader(const Hunk& hunk)
        {
            CString text;
            text.Format(L"@@ -%d,%d +%d,%d @@ %s", hunk.oldStart, hunk.oldCount, hunk.newStart, hunk.newCount, hunk.section.c_str());
            return text;
        }
    }

    // ---- PatchTextView ----

    BEGIN_MESSAGE_MAP(PatchTextView, CWnd)
        ON_WM_PAINT()
        ON_WM_ERASEBKGND()
        ON_WM_SIZE()
        ON_WM_VSCROLL()
        ON_WM_MOUSEWHEEL()
    END_MESSAGE_MAP()

    BOOL PatchTextView::Create(CWnd* parent)
    {
        const CString className = AfxRegisterWndClass(0, ::LoadCursor(nullptr, IDC_ARROW));
        return CWnd::Create(className, L"Patch text", WS_CHILD | WS_VISIBLE | WS_VSCROLL, CRect(), parent, kText);
    }

    void PatchTextView::SetLines(std::vector<std::pair<wchar_t, std::wstring>> lines)
    {
        m_lines = std::move(lines);
        m_top = 0;
        UpdateScrollBar();
        Invalidate(FALSE);
    }

    int PatchTextView::RowHeight() const
    {
        CClientDC dc(const_cast<PatchTextView*>(this));
        CFont* old = dc.SelectObject(&GetFonts().code);
        TEXTMETRICW tm = {};
        dc.GetTextMetrics(&tm);
        dc.SelectObject(old);
        return tm.tmHeight + GetFonts().Scale(3);
    }

    void PatchTextView::UpdateScrollBar()
    {
        if (!m_hWnd)
            return;
        CRect client;
        GetClientRect(&client);
        const int visible = (std::max)(1, client.Height() / RowHeight());
        SCROLLINFO info = { sizeof(info), SIF_ALL | SIF_DISABLENOSCROLL, 0, (std::max)(0, static_cast<int>(m_lines.size()) - 1), static_cast<UINT>(visible), m_top, 0 };
        SetScrollInfo(SB_VERT, &info, TRUE);
    }

    BOOL PatchTextView::OnEraseBkgnd(CDC*)
    {
        return TRUE;
    }

    void PatchTextView::OnSize(UINT type, int cx, int cy)
    {
        CWnd::OnSize(type, cx, cy);
        UpdateScrollBar();
    }

    void PatchTextView::OnPaint()
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
        const int rowHeight = RowHeight();
        const int left = GetFonts().Scale(8);
        for (int row = m_top; row < static_cast<int>(m_lines.size()); ++row)
        {
            const int y = (row - m_top) * rowHeight;
            if (y > client.bottom)
                break;
            const auto& [kind, text] = m_lines[static_cast<size_t>(row)];
            COLORREF back = theme.paneBack, color = theme.text;
            switch (kind)
            {
            case L'+':
                back = theme.insertBack;
                break;
            case L'-':
                back = theme.removeBack;
                break;
            case L'@':
                back = theme.changeBack;
                color = theme.keyword;
                break;
            case L'f':
                back = theme.pathBarBack;
                color = theme.accent;
                break;
            default:
                break;
            }
            dc.FillSolidRect(0, y, client.Width(), rowHeight, back);
            dc.SetTextColor(color);
            CRect r(left, y, client.right, y + rowHeight);
            dc.DrawText(text.c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_EXPANDTABS);
        }
        dc.SelectObject(oldFont);
        paint.BitBlt(0, 0, client.Width(), client.Height(), &dc, 0, 0, SRCCOPY);
        dc.SelectObject(oldBitmap);
    }

    void PatchTextView::OnVScroll(UINT code, UINT, CScrollBar*)
    {
        SCROLLINFO info = { sizeof(info), SIF_ALL };
        GetScrollInfo(SB_VERT, &info);
        int top = m_top;
        if (code == SB_LINEUP)
            --top;
        else if (code == SB_LINEDOWN)
            ++top;
        else if (code == SB_PAGEUP)
            top -= static_cast<int>(info.nPage);
        else if (code == SB_PAGEDOWN)
            top += static_cast<int>(info.nPage);
        else if (code == SB_THUMBTRACK || code == SB_THUMBPOSITION)
            top = info.nTrackPos;
        m_top = (std::max)(0, (std::min)(top, static_cast<int>(m_lines.size()) - 1));
        UpdateScrollBar();
        Invalidate(FALSE);
    }

    BOOL PatchTextView::OnMouseWheel(UINT, short delta, CPoint)
    {
        m_top = (std::max)(0, (std::min)(m_top - delta * 3 / WHEEL_DELTA, static_cast<int>(m_lines.size()) - 1));
        UpdateScrollBar();
        Invalidate(FALSE);
        return TRUE;
    }

    // ---- PatchWnd ----

    BEGIN_MESSAGE_MAP(PatchWnd, TabWindow)
        ON_WM_PAINT()
        ON_WM_ERASEBKGND()
        ON_WM_SIZE()
        ON_WM_SETFOCUS()
        ON_COMMAND(ID_VIEW_RECOMPARE, &PatchWnd::OnReload)
        ON_COMMAND(ID_VIEW_START, &PatchWnd::OnReload)
        ON_UPDATE_COMMAND_UI(ID_VIEW_RECOMPARE, &PatchWnd::OnUpdateAlways)
        ON_UPDATE_COMMAND_UI(ID_VIEW_START, &PatchWnd::OnUpdateAlways)
    END_MESSAGE_MAP()

    PatchWnd::PatchWnd() = default;
    PatchWnd::~PatchWnd() = default;

    BOOL PatchWnd::Create(CWnd* parent, UINT id)
    {
        const CString className = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW));
        if (!CWnd::Create(className, L"Patch", WS_CHILD | WS_CLIPCHILDREN, CRect(), parent, id))
            return FALSE;
        const DWORD child = WS_CHILD | WS_VISIBLE;
        m_folderLabel.Create(L"Apply to folder:", child | SS_CENTERIMAGE, CRect(), this);
        m_folder.CreateEx(WS_EX_CLIENTEDGE, L"EDIT", L"", child | WS_TABSTOP | ES_AUTOHSCROLL, CRect(), this, kFolder);
        m_browse.Create(L"Browse...", child | WS_TABSTOP | BS_PUSHBUTTON, CRect(), this, kBrowse);
        m_stripLabel.Create(L"Strip path components:", child | SS_CENTERIMAGE, CRect(), this);
        m_strip.CreateEx(WS_EX_CLIENTEDGE, L"EDIT", L"1", child | WS_TABSTOP | ES_NUMBER, CRect(), this, kStrip);
        m_reverse.Create(L"Reverse (undo the patch)", child | WS_TABSTOP | BS_AUTOCHECKBOX, CRect(), this, kReverse);
        m_all.Create(L"Check All", child | WS_TABSTOP | BS_PUSHBUTTON, CRect(), this, kAll);
        m_none.Create(L"Check None", child | WS_TABSTOP | BS_PUSHBUTTON, CRect(), this, kNone);
        m_apply.Create(L"Apply Checked Hunks", child | WS_TABSTOP | BS_DEFPUSHBUTTON, CRect(), this, kApply);
        m_list.Create(child | WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS, CRect(), this, kList);
        m_list.SetExtendedStyle(LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        m_text.Create(this);
        UpdateMetrics();
        return TRUE;
    }

    void PatchWnd::UpdateMetrics()
    {
        CFont* font = &GetFonts().ui;
        for (CWnd* control : std::initializer_list<CWnd*>{ &m_folderLabel, &m_folder, &m_browse, &m_stripLabel, &m_strip, &m_reverse, &m_all, &m_none, &m_apply, &m_list })
            control->SetFont(font);
        if (m_list.GetHeaderCtrl() && m_list.GetHeaderCtrl()->GetItemCount() == 0)
        {
            m_list.InsertColumn(0, L"File / hunk", LVCFMT_LEFT, GetFonts().Scale(330));
            m_list.InsertColumn(1, L"Lines", LVCFMT_LEFT, GetFonts().Scale(70));
            m_list.InsertColumn(2, L"Result", LVCFMT_LEFT, GetFonts().Scale(170));
        }
        Layout();
    }

    bool PatchWnd::Open(const std::wstring& patchPath, CString& error)
    {
        m_path = patchPath;
        std::string bytes;
        std::wstring message;
        if (!ReadFileBytes(patchPath, bytes, message))
        {
            error.Format(L"Cannot open %s:\n%s", patchPath.c_str(), message.c_str());
            return false;
        }
        m_parseError.clear();
        m_patches = ParsePatch(DecodeText(bytes).text, m_parseError);
        if (m_patches.empty())
        {
            error.Format(L"%s\n\n%s", patchPath.c_str(), m_parseError.c_str());
            return false;
        }
        // git patches (a/..., b/...) need one leading component stripped; plain diffs none.
        bool gitStyle = true;
        for (const FilePatch& patch : m_patches)
        {
            const std::wstring& path = patch.isNew ? patch.newPath : patch.oldPath;
            gitStyle = gitStyle && (path.compare(0, 2, L"a/") == 0 || path.compare(0, 2, L"b/") == 0);
        }
        m_strip.SetWindowTextW(gitStyle ? L"1" : L"0");
        if (m_folder.GetWindowTextLengthW() == 0)
            m_folder.SetWindowTextW(ParentFolder(patchPath).c_str());
        m_results.clear();
        for (const FilePatch& patch : m_patches)
            m_results.emplace_back(patch.hunks.size());
        FillList();
        return true;
    }

    void PatchWnd::FillList()
    {
        m_updatingChecks = true;
        m_list.DeleteAllItems();
        m_rows.clear();
        int index = 0;
        for (size_t f = 0; f < m_patches.size(); ++f)
        {
            const FilePatch& patch = m_patches[f];
            int added = 0, removed = 0;
            for (const Hunk& hunk : patch.hunks)
            {
                added += hunk.Added();
                removed += hunk.Removed();
            }
            CString name = (patch.isNew ? patch.newPath : patch.oldPath).c_str();
            if (patch.isNew)
                name += L"  (new file)";
            else if (patch.isDeleted)
                name += L"  (deleted)";
            CString lines;
            lines.Format(L"+%d \x2212%d", added, removed);
            m_list.InsertItem(index, name);
            m_list.SetItemText(index, 1, lines);
            m_list.SetCheck(index, TRUE);
            m_rows.push_back({ static_cast<int>(f), -1 });
            ++index;
            for (size_t h = 0; h < patch.hunks.size(); ++h)
            {
                const Hunk& hunk = patch.hunks[h];
                m_list.InsertItem(index, L"      " + HunkHeader(hunk));
                lines.Format(L"+%d \x2212%d", hunk.Added(), hunk.Removed());
                m_list.SetItemText(index, 1, lines);
                m_list.SetItemText(index, 2, m_results[f][h]);
                m_list.SetCheck(index, TRUE);
                m_rows.push_back({ static_cast<int>(f), static_cast<int>(h) });
                ++index;
            }
        }
        m_updatingChecks = false;
        if (!m_rows.empty())
        {
            m_list.SetItemState(0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ShowRow(0);
        }
        NotifyFrame();
    }

    void PatchWnd::ShowRow(int row)
    {
        if (row < 0 || row >= static_cast<int>(m_rows.size()))
            return;
        const Row& info = m_rows[static_cast<size_t>(row)];
        const FilePatch& patch = m_patches[static_cast<size_t>(info.file)];
        std::vector<std::pair<wchar_t, std::wstring>> lines;
        lines.emplace_back(L'f', L"--- " + patch.oldPath);
        lines.emplace_back(L'f', L"+++ " + patch.newPath);
        for (size_t h = 0; h < patch.hunks.size(); ++h)
        {
            if (info.hunk >= 0 && static_cast<int>(h) != info.hunk)
                continue;
            const Hunk& hunk = patch.hunks[h];
            lines.emplace_back(L'@', std::wstring(HunkHeader(hunk)));
            for (const PatchLine& line : hunk.lines)
            {
                lines.emplace_back(line.op, std::wstring(1, line.op) + line.text);
                if (line.noNewline)
                    lines.emplace_back(L' ', L"\\ No newline at end of file");
            }
        }
        m_text.SetLines(std::move(lines));
    }

    bool PatchWnd::IsChecked(int file, int hunk) const
    {
        for (size_t row = 0; row < m_rows.size(); ++row)
        {
            if (m_rows[row].file == file && m_rows[row].hunk == hunk)
                return m_list.GetCheck(static_cast<int>(row)) != FALSE;
        }
        return false;
    }

    void PatchWnd::CheckAll(bool check)
    {
        m_updatingChecks = true;
        for (int row = 0; row < m_list.GetItemCount(); ++row)
            m_list.SetCheck(row, check);
        m_updatingChecks = false;
    }

    BOOL PatchWnd::OnNotify(WPARAM wParam, LPARAM lParam, LRESULT* result)
    {
        const NMHDR* header = reinterpret_cast<const NMHDR*>(lParam);
        if (header->idFrom == kList && header->code == LVN_ITEMCHANGED)
        {
            const NMLISTVIEW* change = reinterpret_cast<const NMLISTVIEW*>(lParam);
            if ((change->uNewState & LVIS_SELECTED) && !(change->uOldState & LVIS_SELECTED))
                ShowRow(change->iItem);
            // Checking a file's row checks (or clears) all of its hunks.
            const bool checkChanged = ((change->uNewState ^ change->uOldState) & LVIS_STATEIMAGEMASK) != 0 && (change->uOldState & LVIS_STATEIMAGEMASK) != 0;
            if (checkChanged && !m_updatingChecks && change->iItem >= 0 && change->iItem < static_cast<int>(m_rows.size()) &&
                m_rows[static_cast<size_t>(change->iItem)].hunk < 0)
            {
                const BOOL checked = m_list.GetCheck(change->iItem);
                const int file = m_rows[static_cast<size_t>(change->iItem)].file;
                m_updatingChecks = true;
                for (size_t row = 0; row < m_rows.size(); ++row)
                {
                    if (m_rows[row].file == file)
                        m_list.SetCheck(static_cast<int>(row), checked);
                }
                m_updatingChecks = false;
            }
        }
        return TabWindow::OnNotify(wParam, lParam, result);
    }

    BOOL PatchWnd::OnCommand(WPARAM wParam, LPARAM lParam)
    {
        if (HIWORD(wParam) == BN_CLICKED)
        {
            switch (LOWORD(wParam))
            {
            case kBrowse:
                Browse();
                return TRUE;
            case kApply:
                Apply();
                return TRUE;
            case kAll:
                CheckAll(true);
                return TRUE;
            case kNone:
                CheckAll(false);
                return TRUE;
            default:
                break;
            }
        }
        return TabWindow::OnCommand(wParam, lParam);
    }

    std::wstring PatchWnd::Folder() const
    {
        CString text;
        m_folder.GetWindowTextW(text);
        text.Trim();
        text.TrimRight(L"\\/");
        return std::wstring(text);
    }

    void PatchWnd::Browse()
    {
        CFolderPickerDialog dialog(Folder().c_str(), 0, this);
        if (dialog.DoModal() == IDOK)
            m_folder.SetWindowTextW(dialog.GetPathName());
    }

    void PatchWnd::Apply()
    {
        const std::wstring folder = Folder();
        const DWORD attributes = GetFileAttributesW(folder.c_str());
        if (folder.empty() || attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            AfxMessageBox(L"Choose the folder the patch applies to.", MB_ICONINFORMATION);
            m_folder.SetFocus();
            return;
        }
        CString stripText;
        m_strip.GetWindowTextW(stripText);
        const int strip = _wtoi(stripText);
        const bool reverse = m_reverse.GetCheck() == BST_CHECKED;
        MainFrame* frame = dynamic_cast<MainFrame*>(GetParentFrame());
        int appliedHunks = 0, failedHunks = 0, files = 0;
        CString notes;
        for (size_t f = 0; f < m_patches.size(); ++f)
        {
            FilePatch patch = reverse ? ReversePatch(m_patches[f]) : m_patches[f];
            std::vector<bool> selected;
            bool any = false;
            for (size_t h = 0; h < patch.hunks.size(); ++h)
            {
                selected.push_back(IsChecked(static_cast<int>(f), static_cast<int>(h)));
                any = any || selected.back();
            }
            if (!any)
                continue;
            const std::wstring target = folder + L"\\" + PatchTarget(patch, strip);
            if (patch.isDeleted)
            {
                notes += (L"\n" + target + L": the patch deletes this file; delete it yourself if you want to.").c_str();
                continue;
            }
            std::string bytes;
            std::wstring error;
            const bool exists = GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES;
            if (!patch.isNew && (!exists || !ReadFileBytes(target, bytes, error)))
            {
                notes += (L"\n" + target + L": not found (check the folder and the strip setting).").c_str();
                for (size_t h = 0; h < patch.hunks.size(); ++h)
                    m_results[f][h] = selected[h] ? L"File not found" : L"";
                failedHunks += static_cast<int>(std::count(selected.begin(), selected.end(), true));
                continue;
            }
            std::vector<HunkResult> results;
            const std::wstring patched = ApplyPatch(exists ? DecodeText(bytes).text : std::wstring(), patch, selected, results);
            int applied = 0;
            for (size_t h = 0; h < results.size(); ++h)
            {
                CString text;
                if (results[h].skipped)
                    text = L"Not checked";
                else if (!results[h].applied)
                    text = L"FAILED: context not found";
                else if (results[h].offset != 0 || results[h].fuzzy)
                    text.Format(L"Applied at line %d (offset %+d)%s", results[h].line, results[h].offset, results[h].fuzzy ? L", whitespace" : L"");
                else
                    text.Format(L"Applied at line %d", results[h].line);
                m_results[f][h] = text;
                applied += results[h].applied ? 1 : 0;
                failedHunks += !results[h].skipped && !results[h].applied ? 1 : 0;
            }
            appliedHunks += applied;
            if (applied == 0 || !frame)
                continue;
            // Review tab: the file as it is on the left, patched (unsaved) on the right.
            ++files;
            if (!frame->OpenComparison(exists ? std::vector<std::wstring>{ target, target } : std::vector<std::wstring>{ L"", L"" }))
                continue;
            CompareWnd* review = frame->ActiveText();
            if (!review)
                continue;
            Comparison& doc = review->Doc();
            if (!exists)
            {
                doc.Buffer(0).title = LeafName(target) + L" (new file)";
                doc.Buffer(1).path = target;
            }
            doc.ReplaceRange(1, TextPos(), doc.Buffer(1).EndPos(), patched);
            doc.Buffer(1).title = LeafName(target) + L" (patched)";
            review->OnTextChanged();
            review->GoToFirstDifference();
        }
        FillList();
        m_lastSummary.Format(L"%d hunk(s) applied, %d failed, %d file(s) opened for review.", appliedHunks, failedHunks, files);
        CString message = m_lastSummary;
        if (files > 0)
            message += L"\n\nEach patched file is open in its own tab: the original is on the left and the patched text, not yet saved, on the right. "
                       L"Review it and save the right side (Ctrl+S).";
        if (!notes.IsEmpty())
            message += L"\n" + notes;
        AfxMessageBox(message, failedHunks ? MB_ICONEXCLAMATION : MB_ICONINFORMATION);
        NotifyFrame();
    }

    void PatchWnd::OnReload()
    {
        CString error;
        if (!Open(m_path, error))
            AfxMessageBox(error, MB_ICONERROR);
    }

    void PatchWnd::OnUpdateAlways(CCmdUI* ui)
    {
        ui->Enable(TRUE);
    }

    TabItem PatchWnd::GetTabItem() const
    {
        TabItem item;
        item.kind = TabKind::Patch;
        item.title = LeafName(m_path).c_str();
        item.tooltip = m_path.c_str();
        return item;
    }

    CString PatchWnd::StatusText(int part) const
    {
        if (part == 0)
            return L"Patch file (unified diff)";
        if (part == 1)
        {
            int hunks = 0;
            for (const FilePatch& patch : m_patches)
                hunks += static_cast<int>(patch.hunks.size());
            CString text;
            text.Format(L"%d file(s) \x2022 %d hunk(s)", static_cast<int>(m_patches.size()), hunks);
            return text;
        }
        return m_lastSummary.IsEmpty() ? CString(m_parseError.c_str()) : m_lastSummary;
    }

    void PatchWnd::NotifyFrame()
    {
        if (CFrameWnd* frame = GetParentFrame())
            frame->PostMessage(WM_MF_STATECHANGED);
    }

    void PatchWnd::Layout()
    {
        if (!m_hWnd || !m_list.m_hWnd)
            return;
        CRect client;
        GetClientRect(&client);
        const Fonts& fonts = GetFonts();
        const int pad = fonts.Scale(8);
        const int row = fonts.Scale(26);
        int x = pad;
        const int y = pad;
        auto place = [&](CWnd& control, int width) {
            control.MoveWindow(x, y, width, row);
            x += width + fonts.Scale(6);
        };
        place(m_folderLabel, fonts.Scale(100));
        place(m_folder, (std::max)(fonts.Scale(160), static_cast<int>(client.Width()) - fonts.Scale(880)));
        place(m_browse, fonts.Scale(80));
        place(m_stripLabel, fonts.Scale(140));
        place(m_strip, fonts.Scale(36));
        place(m_reverse, fonts.Scale(170));
        place(m_apply, fonts.Scale(150));
        const int top = y + row + pad;
        const int listWidth = (std::min)(fonts.Scale(600), static_cast<int>(client.Width()) / 2);
        const int buttons = fonts.Scale(30);
        m_list.MoveWindow(pad, top, listWidth, (std::max)(0, static_cast<int>(client.Height()) - top - buttons - pad));
        m_all.MoveWindow(pad, client.bottom - buttons, fonts.Scale(100), fonts.Scale(24));
        m_none.MoveWindow(pad + fonts.Scale(106), client.bottom - buttons, fonts.Scale(100), fonts.Scale(24));
        m_text.MoveWindow(pad * 2 + listWidth, top, (std::max)(0, static_cast<int>(client.Width()) - listWidth - pad * 3),
            (std::max)(0, static_cast<int>(client.Height()) - top - pad));
        Invalidate(FALSE);
    }

    void PatchWnd::OnSize(UINT type, int cx, int cy)
    {
        TabWindow::OnSize(type, cx, cy);
        Layout();
    }

    void PatchWnd::OnSetFocus(CWnd*)
    {
        m_list.SetFocus();
    }

    BOOL PatchWnd::OnEraseBkgnd(CDC*)
    {
        return TRUE;
    }

    void PatchWnd::OnPaint()
    {
        CPaintDC dc(this);
        CRect client;
        GetClientRect(&client);
        dc.FillSolidRect(&client, GetTheme().chromeBack);
    }
}
