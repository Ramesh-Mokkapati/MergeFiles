// CompareWnd.cpp - layout, synchronized scrolling and the commands of one comparison.
#include "CompareWnd.h"

#include "Archive.h"
#include "Dialogs.h"
#include "LineDetailPanel.h"
#include "LinkGutter.h"
#include "MainFrame.h"
#include "MergeFilesApp.h"
#include "XmlFormat.h"
#include "Output.h"
#include "OverviewStrip.h"
#include "Printing.h"
#include "PaneView.h"
#include "Theme.h"
#include "resource.h"

namespace mf
{
    namespace
    {
        const wchar_t kLockGlyph = 0xE72E;
        const wchar_t kMoreGlyph = 0xE712;

        CString FileName(const TextBuffer& buffer)
        {
            return CString(buffer.title.c_str());
        }

        CString Plural(int count, const wchar_t* one, const wchar_t* many)
        {
            CString text;
            text.Format(L"%d %s", count, count == 1 ? one : many);
            return text;
        }
    }

    BEGIN_MESSAGE_MAP(CompareWnd, CWnd)
        ON_WM_PAINT()
        ON_WM_ERASEBKGND()
        ON_WM_SIZE()
        ON_WM_LBUTTONDOWN()
        ON_WM_LBUTTONDBLCLK()
        ON_WM_SETFOCUS()
        ON_COMMAND(ID_FILE_SAVE_PANE, &CompareWnd::OnSavePane)
        ON_COMMAND(ID_FILE_SAVE_PANE_AS, &CompareWnd::OnSavePaneAs)
        ON_COMMAND(ID_FILE_SAVE_ALL_PANES, &CompareWnd::OnSaveAll)
        ON_COMMAND(ID_FILE_OPEN_IN_PANE, &CompareWnd::OnOpenInPane)
        ON_UPDATE_COMMAND_UI(ID_FILE_SAVE_PANE, &CompareWnd::OnUpdateSavePane)
        ON_UPDATE_COMMAND_UI(ID_FILE_SAVE_ALL_PANES, &CompareWnd::OnUpdateSaveAll)
        ON_UPDATE_COMMAND_UI(ID_FILE_SAVE_PANE_AS, &CompareWnd::OnUpdateAlways)
        ON_UPDATE_COMMAND_UI(ID_FILE_OPEN_IN_PANE, &CompareWnd::OnUpdateAlways)
        ON_COMMAND(ID_EDIT_UNDO, &CompareWnd::OnUndo)
        ON_COMMAND(ID_EDIT_REDO, &CompareWnd::OnRedo)
        ON_UPDATE_COMMAND_UI(ID_EDIT_UNDO, &CompareWnd::OnUpdateUndo)
        ON_UPDATE_COMMAND_UI(ID_EDIT_REDO, &CompareWnd::OnUpdateRedo)
        ON_COMMAND(ID_EDIT_CUT, &CompareWnd::OnCut)
        ON_COMMAND(ID_EDIT_COPY, &CompareWnd::OnCopy)
        ON_COMMAND(ID_EDIT_PASTE, &CompareWnd::OnPaste)
        ON_COMMAND(ID_EDIT_SELECT_ALL, &CompareWnd::OnSelectAll)
        ON_UPDATE_COMMAND_UI(ID_EDIT_CUT, &CompareWnd::OnUpdateCut)
        ON_UPDATE_COMMAND_UI(ID_EDIT_COPY, &CompareWnd::OnUpdateCopy)
        ON_UPDATE_COMMAND_UI(ID_EDIT_PASTE, &CompareWnd::OnUpdatePaste)
        ON_UPDATE_COMMAND_UI(ID_EDIT_SELECT_ALL, &CompareWnd::OnUpdateAlways)
        ON_COMMAND(ID_EDIT_GOTO_LINE, &CompareWnd::OnGoToLine)
        ON_UPDATE_COMMAND_UI(ID_EDIT_GOTO_LINE, &CompareWnd::OnUpdateAlways)
        ON_COMMAND(ID_EDIT_INDENT, &CompareWnd::OnIndent)
        ON_COMMAND(ID_EDIT_OUTDENT, &CompareWnd::OnOutdent)
        ON_UPDATE_COMMAND_UI(ID_EDIT_INDENT, &CompareWnd::OnUpdateEditable)
        ON_UPDATE_COMMAND_UI(ID_EDIT_OUTDENT, &CompareWnd::OnUpdateEditable)
        ON_COMMAND(ID_VIEW_START, &CompareWnd::OnStart)
        ON_UPDATE_COMMAND_UI(ID_VIEW_START, &CompareWnd::OnUpdateStart)
        ON_UPDATE_COMMAND_UI(ID_VIEW_STOP, &CompareWnd::OnUpdateStop)
        ON_COMMAND(ID_VIEW_RECOMPARE, &CompareWnd::OnRecompare)
        ON_UPDATE_COMMAND_UI(ID_VIEW_RECOMPARE, &CompareWnd::OnUpdateAlways)
        ON_COMMAND(ID_VIEW_LAYOUT_TWO, &CompareWnd::OnLayoutTwo)
        ON_COMMAND(ID_VIEW_LAYOUT_THREE, &CompareWnd::OnLayoutThree)
        ON_UPDATE_COMMAND_UI(ID_VIEW_LAYOUT_TWO, &CompareWnd::OnUpdateLayoutTwo)
        ON_UPDATE_COMMAND_UI(ID_VIEW_LAYOUT_THREE, &CompareWnd::OnUpdateLayoutThree)
        ON_COMMAND(ID_VIEW_STATISTICS, &CompareWnd::OnStatistics)
        ON_UPDATE_COMMAND_UI(ID_VIEW_STATISTICS, &CompareWnd::OnUpdateAlways)
        ON_COMMAND(ID_MERGE_NEXT_CHANGE, &CompareWnd::OnNextChange)
        ON_COMMAND(ID_MERGE_PREV_CHANGE, &CompareWnd::OnPrevChange)
        ON_COMMAND(ID_MERGE_NEXT_CONFLICT, &CompareWnd::OnNextConflict)
        ON_COMMAND(ID_MERGE_PREV_CONFLICT, &CompareWnd::OnPrevConflict)
        ON_UPDATE_COMMAND_UI(ID_MERGE_NEXT_CHANGE, &CompareWnd::OnUpdateNextChange)
        ON_UPDATE_COMMAND_UI(ID_MERGE_PREV_CHANGE, &CompareWnd::OnUpdatePrevChange)
        ON_UPDATE_COMMAND_UI(ID_MERGE_NEXT_CONFLICT, &CompareWnd::OnUpdateNextConflict)
        ON_UPDATE_COMMAND_UI(ID_MERGE_PREV_CONFLICT, &CompareWnd::OnUpdatePrevConflict)
        ON_COMMAND(ID_MERGE_COPY_TO_RIGHT, &CompareWnd::OnCopyToRight)
        ON_COMMAND(ID_MERGE_COPY_TO_LEFT, &CompareWnd::OnCopyToLeft)
        ON_UPDATE_COMMAND_UI(ID_MERGE_COPY_TO_RIGHT, &CompareWnd::OnUpdateCopyToRight)
        ON_UPDATE_COMMAND_UI(ID_MERGE_COPY_TO_LEFT, &CompareWnd::OnUpdateCopyToLeft)
        ON_COMMAND(ID_MERGE_AUTO, &CompareWnd::OnAutoMerge)
        ON_UPDATE_COMMAND_UI(ID_MERGE_AUTO, &CompareWnd::OnUpdateAutoMerge)
        ON_COMMAND(ID_MERGE_MARK_RESOLVED, &CompareWnd::OnMarkResolved)
        ON_COMMAND(ID_MERGE_UNMARK_RESOLVED, &CompareWnd::OnUnmarkResolved)
        ON_UPDATE_COMMAND_UI(ID_MERGE_MARK_RESOLVED, &CompareWnd::OnUpdateAlways)
        ON_UPDATE_COMMAND_UI(ID_MERGE_UNMARK_RESOLVED, &CompareWnd::OnUpdateAlways)
        ON_COMMAND(ID_MERGE_REPLACE_RIGHT, &CompareWnd::OnReplaceRight)
        ON_COMMAND(ID_MERGE_REPLACE_LEFT, &CompareWnd::OnReplaceLeft)
        ON_UPDATE_COMMAND_UI(ID_MERGE_REPLACE_RIGHT, &CompareWnd::OnUpdateReplaceRight)
        ON_UPDATE_COMMAND_UI(ID_MERGE_REPLACE_LEFT, &CompareWnd::OnUpdateReplaceLeft)
        ON_COMMAND_RANGE(ID_ENC_UTF8, ID_ENC_ANSI, &CompareWnd::OnEncoding)
        ON_UPDATE_COMMAND_UI_RANGE(ID_ENC_UTF8, ID_ENC_ANSI, &CompareWnd::OnUpdateEncoding)
        ON_COMMAND_RANGE(ID_REOPEN_AUTO, ID_REOPEN_UTF16LE, &CompareWnd::OnReopen)
        ON_UPDATE_COMMAND_UI_RANGE(ID_REOPEN_AUTO, ID_REOPEN_UTF16LE, &CompareWnd::OnUpdateReopen)
        ON_COMMAND_RANGE(ID_EOL_CRLF, ID_EOL_CR, &CompareWnd::OnEol)
        ON_UPDATE_COMMAND_UI_RANGE(ID_EOL_CRLF, ID_EOL_CR, &CompareWnd::OnUpdateEol)
        ON_COMMAND(ID_VIEW_FORMAT_XML, &CompareWnd::OnFormatXml)
        ON_UPDATE_COMMAND_UI(ID_VIEW_FORMAT_XML, &CompareWnd::OnUpdateFormatXml)
        ON_COMMAND(ID_BOOKMARK_TOGGLE, &CompareWnd::OnToggleBookmark)
        ON_COMMAND(ID_BOOKMARK_NEXT, &CompareWnd::OnNextBookmark)
        ON_COMMAND(ID_BOOKMARK_PREV, &CompareWnd::OnPrevBookmark)
        ON_COMMAND(ID_BOOKMARK_CLEAR, &CompareWnd::OnClearBookmarks)
        ON_UPDATE_COMMAND_UI(ID_BOOKMARK_TOGGLE, &CompareWnd::OnUpdateAlways)
        ON_UPDATE_COMMAND_UI(ID_BOOKMARK_NEXT, &CompareWnd::OnUpdateBookmarks)
        ON_UPDATE_COMMAND_UI(ID_BOOKMARK_PREV, &CompareWnd::OnUpdateBookmarks)
        ON_UPDATE_COMMAND_UI(ID_BOOKMARK_CLEAR, &CompareWnd::OnUpdateBookmarks)
        ON_COMMAND(ID_SYNC_ADD, &CompareWnd::OnSyncAdd)
        ON_COMMAND(ID_SYNC_CLEAR, &CompareWnd::OnSyncClear)
        ON_COMMAND(ID_BOOKMARK_COMMENT, &CompareWnd::OnBookmarkComment)
        ON_UPDATE_COMMAND_UI(ID_BOOKMARK_COMMENT, &CompareWnd::OnUpdateAlways)
        ON_COMMAND(ID_FILE_PRINT, &CompareWnd::OnPrint)
        ON_COMMAND(ID_FILE_PRINT_PREVIEW, &CompareWnd::OnPrintPreview)
        ON_COMMAND(ID_FILE_REPORT, &CompareWnd::OnReport)
        ON_COMMAND(ID_FILE_SAVE_ARCHIVE, &CompareWnd::OnSaveArchive)
        ON_UPDATE_COMMAND_UI(ID_FILE_PRINT, &CompareWnd::OnUpdateAlways)
        ON_UPDATE_COMMAND_UI(ID_FILE_PRINT_PREVIEW, &CompareWnd::OnUpdateAlways)
        ON_UPDATE_COMMAND_UI(ID_FILE_REPORT, &CompareWnd::OnUpdateAlways)
        ON_UPDATE_COMMAND_UI(ID_FILE_SAVE_ARCHIVE, &CompareWnd::OnUpdateAlways)
        ON_UPDATE_COMMAND_UI(ID_SYNC_ADD, &CompareWnd::OnUpdateAlways)
        ON_UPDATE_COMMAND_UI(ID_SYNC_CLEAR, &CompareWnd::OnUpdateSyncClear)
    END_MESSAGE_MAP()

    CompareWnd::CompareWnd() = default;
    CompareWnd::~CompareWnd() = default;

    BOOL CompareWnd::Create(CWnd* parent, UINT id)
    {
        const CString className = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW));
        if (!CWnd::Create(className, L"Comparison", WS_CHILD | WS_CLIPCHILDREN, CRect(), parent, id))
            return FALSE;
        CreateChildren();
        return TRUE;
    }

    void CompareWnd::CreateChildren()
    {
        for (auto& pane : m_panes)
            pane->DestroyWindow();
        for (auto& gutter : m_gutters)
            gutter->DestroyWindow();
        m_panes.clear();
        m_gutters.clear();
        if (m_overviewLeft)
            m_overviewLeft->DestroyWindow();
        if (m_overviewRight)
            m_overviewRight->DestroyWindow();

        const int count = m_doc.PaneCount();
        for (int pane = 0; pane < count; ++pane)
        {
            m_panes.push_back(std::make_unique<PaneView>(*this, pane));
            m_panes.back()->Create(this);
        }
        for (int pane = 0; pane + 1 < count; ++pane)
        {
            m_gutters.push_back(std::make_unique<LinkGutter>(*this, pane));
            m_gutters.back()->Create(this);
        }
        m_overviewLeft = std::make_unique<OverviewStrip>(*this, 0);
        m_overviewLeft->Create(this);
        m_overviewRight = std::make_unique<OverviewStrip>(*this, count - 1);
        m_overviewRight->Create(this);
        if (!m_lineDetail)
        {
            m_lineDetail = std::make_unique<LineDetailPanel>(*this);
            m_lineDetail->Create(this);
        }
        m_active = (std::min)(m_active, count - 1);
        m_rowsRevision = 0;
        Layout();
    }

    void CompareWnd::Layout()
    {
        if (!m_hWnd || m_panes.empty())
            return;
        CRect client;
        GetClientRect(&client);
        const Fonts& fonts = GetFonts();
        const int pathHeight = fonts.Scale(30);
        const int overview = fonts.Scale(14);
        const int gutter = fonts.Scale(46);
        const int count = static_cast<int>(m_panes.size());
        const int top = pathHeight;
        const int detailHeight = m_view.lineDetail && m_lineDetail ? m_lineDetail->PreferredHeight(PaneCount()) : 0;
        const int bottom = (std::max)(top, static_cast<int>(client.bottom) - detailHeight);
        if (m_lineDetail)
        {
            m_lineDetail->MoveWindow(0, bottom, client.Width(), detailHeight);
            m_lineDetail->ShowWindow(detailHeight > 0 ? SW_SHOW : SW_HIDE);
        }
        const int available = (std::max)(0, client.Width() - 2 * overview - (count - 1) * gutter);
        const int paneWidth = available / count;

        m_overviewLeft->MoveWindow(0, top, overview, bottom - top);
        int x = overview;
        for (int pane = 0; pane < count; ++pane)
        {
            const int right = pane + 1 == count ? client.right - overview : x + paneWidth;
            m_panes[static_cast<size_t>(pane)]->MoveWindow(x, top, right - x, bottom - top);
            x = right;
            if (pane + 1 < count)
            {
                m_gutters[static_cast<size_t>(pane)]->MoveWindow(x, top, gutter, bottom - top);
                x += gutter;
            }
        }
        m_overviewRight->MoveWindow(client.right - overview, top, overview, bottom - top);
        Invalidate(FALSE);
    }

    CRect CompareWnd::PathBarRect(int pane) const
    {
        CRect rect;
        m_panes[static_cast<size_t>(pane)]->GetWindowRect(&rect);
        ScreenToClient(&rect);
        return CRect(rect.left, 0, rect.right, GetFonts().Scale(30));
    }

    void CompareWnd::OnSize(UINT type, int cx, int cy)
    {
        CWnd::OnSize(type, cx, cy);
        Layout();
    }

    BOOL CompareWnd::OnEraseBkgnd(CDC*)
    {
        return TRUE;
    }

    void CompareWnd::OnPaint()
    {
        CPaintDC dc(this);
        CRect client;
        GetClientRect(&client);
        const Theme& theme = GetTheme();
        Fonts& fonts = GetFonts();
        const int pathHeight = fonts.Scale(30);
        dc.FillSolidRect(0, 0, client.Width(), pathHeight, theme.chromeBack);
        dc.SetBkMode(TRANSPARENT);
        for (int pane = 0; pane < PaneCount(); ++pane)
        {
            const TextBuffer& buffer = m_doc.Buffer(pane);
            CRect bar = PathBarRect(pane);
            bar.DeflateRect(0, fonts.Scale(3), 0, fonts.Scale(3));
            CBrush back(theme.pathBarBack);
            CPen border(PS_SOLID, 1, pane == m_active ? theme.accent : theme.chromeBorder);
            CBrush* ob = dc.SelectObject(&back);
            CPen* op = dc.SelectObject(&border);
            dc.RoundRect(&bar, CPoint(fonts.Scale(6), fonts.Scale(6)));
            dc.SelectObject(ob);
            dc.SelectObject(op);

            CRect text = bar;
            text.DeflateRect(fonts.Scale(8), 0);
            // The "..." menu at the right end.
            CRect more(text.right - fonts.Scale(20), bar.top, text.right, bar.bottom);
            DrawGlyph(dc, more, kMoreGlyph, theme.chromeText, fonts.iconsSmall);
            text.right = more.left - fonts.Scale(4);

            if (buffer.readOnly)
            {
                CRect lock(text.left, bar.top, text.left + fonts.Scale(16), bar.bottom);
                DrawGlyph(dc, lock, kLockGlyph, theme.chromeLabel, fonts.iconsSmall);
                text.left = lock.right + fonts.Scale(4);
            }

            CFont* oldFont = dc.SelectObject(&fonts.uiSmall);
            CString badge;
            if (m_doc.IsThreeWay())
                badge = pane == 0 ? L"LEFT" : (pane == 1 ? L"ANCESTOR \x00B7 MERGE" : L"RIGHT");
            if (m_doc.IsModified(pane))
                badge = badge.IsEmpty() ? CString(L"MODIFIED") : badge + L" \x00B7 MODIFIED";
            if (!badge.IsEmpty())
            {
                const int width = dc.GetTextExtent(badge).cx;
                CRect badgeRect(text.right - width, bar.top, text.right, bar.bottom);
                dc.SetTextColor(m_doc.IsModified(pane) ? theme.accent : theme.chromeLabel);
                dc.DrawText(badge, &badgeRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                text.right = badgeRect.left - fonts.Scale(8);
            }
            dc.SelectObject(&fonts.ui);
            dc.SetTextColor(theme.text);
            CString path = buffer.path.empty() ? CString(buffer.title.c_str()) : CString(buffer.path.c_str());
            dc.DrawText(path, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
            dc.SelectObject(oldFont);
        }
    }

    void CompareWnd::OnLButtonDown(UINT, CPoint point)
    {
        for (int pane = 0; pane < PaneCount(); ++pane)
        {
            const CRect bar = PathBarRect(pane);
            if (!bar.PtInRect(point))
                continue;
            m_panes[static_cast<size_t>(pane)]->SetFocus();
            if (point.x >= bar.right - GetFonts().Scale(30))
            {
                CPoint screen(bar.right - GetFonts().Scale(30), bar.bottom);
                ClientToScreen(&screen);
                ShowPaneMenu(pane, screen);
            }
            return;
        }
    }

    void CompareWnd::OnLButtonDblClk(UINT, CPoint point)
    {
        for (int pane = 0; pane < PaneCount(); ++pane)
        {
            if (PathBarRect(pane).PtInRect(point))
            {
                m_panes[static_cast<size_t>(pane)]->SetFocus();
                OnOpenInPane();
                return;
            }
        }
    }

    void CompareWnd::OnSetFocus(CWnd*)
    {
        if (PaneView* pane = Pane(m_active))
            pane->SetFocus();
    }

    // ---- files ----

    bool CompareWnd::OpenFiles(const std::vector<std::wstring>& paths, CString& error)
    {
        const int count = paths.size() == 3 ? 3 : 2;
        m_doc.SetPaneCount(count);
        CreateChildren();
        for (int pane = 0; pane < count; ++pane)
        {
            const std::wstring path = static_cast<size_t>(pane) < paths.size() ? paths[static_cast<size_t>(pane)] : std::wstring();
            if (path.empty())
            {
                m_doc.SetText(pane, L"", pane == 1 && count == 3 ? L"Untitled (merge)" : L"Untitled");
                continue;
            }
            std::wstring message;
            if (!m_doc.LoadFile(pane, path, message))
            {
                error.Format(L"Cannot open %s:\n%s", path.c_str(), message.c_str());
                return false;
            }
        }
        for (auto& pane : m_panes)
            pane->SetPane(pane->Pane());
        OnTextChanged();
        return true;
    }

    const wchar_t* CompareWnd::HelpTopic() const
    {
        const std::wstring& title = m_doc.Buffer(0).title;
        if (m_doc.Buffer(0).path.empty() && title.size() > 7 && title.compare(title.size() - 7, 7, L" (text)") == 0)
            return L"documents";
        return m_doc.IsThreeWay() ? L"three-way" : L"file-comparison";
    }

    void CompareWnd::OpenArchive(std::unique_ptr<Comparison> doc)
    {
        m_doc = std::move(*doc);
        // Cached layouts belong to the comparison that was replaced.
        m_rowsRevision = ~0u;
        m_detailsRevision = ~0u;
        m_details.clear();
        CreateChildren();
        for (auto& pane : m_panes)
            pane->SetPane(pane->Pane());
        OnTextChanged();
    }

    void CompareWnd::GoToFirstDifference()
    {
        const int first = m_doc.FindDifference(m_active, -1, true, false);
        if (first >= 0)
            GoToChunk(first);
    }

    bool CompareWnd::LoadIntoPane(int pane, const std::wstring& path, CString& error, const TextEncoding* forced)
    {
        std::wstring message;
        if (!m_doc.LoadFile(pane, path, message, forced))
        {
            error.Format(L"Cannot open %s:\n%s", path.c_str(), message.c_str());
            return false;
        }
        Pane(pane)->SetPane(pane);
        OnTextChanged();
        return true;
    }

    TabItem CompareWnd::GetTabItem() const
    {
        TabItem item;
        CString title, tooltip;
        for (int pane = 0; pane < PaneCount(); ++pane)
        {
            const TextBuffer& buffer = m_doc.Buffer(pane);
            if (!title.IsEmpty())
                title += L", ";
            title += buffer.title.c_str();
            if (!tooltip.IsEmpty())
                tooltip += L"\n";
            tooltip += buffer.path.empty() ? buffer.title.c_str() : buffer.path.c_str();
        }
        item.title = title;
        item.tooltip = tooltip;
        item.modified = m_doc.AnyModified();
        item.kind = TabKind::Text;
        return item;
    }

    bool CompareWnd::QueryClose()
    {
        for (int pane = 0; pane < PaneCount(); ++pane)
        {
            if (!m_doc.IsModified(pane))
                continue;
            CString question;
            question.Format(L"Save changes to %s?", static_cast<LPCWSTR>(FileName(m_doc.Buffer(pane))));
            const int answer = AfxMessageBox(question, MB_YESNOCANCEL | MB_ICONQUESTION);
            if (answer == IDCANCEL)
                return false;
            if (answer == IDYES && !SavePane(pane, false))
                return false;
        }
        return true;
    }

    bool CompareWnd::SavePane(int pane, bool saveAs)
    {
        TextBuffer& buffer = m_doc.Buffer(pane);
        std::wstring path = buffer.path;
        if (!saveAs && !path.empty() && buffer.readOnly)
        {
            if (AfxMessageBox(L"The file is read-only. Save it under another name?", MB_YESNO | MB_ICONQUESTION) != IDYES)
                return false;
            saveAs = true;
        }
        if (saveAs || path.empty())
        {
            CFileDialog dialog(FALSE, nullptr, path.empty() ? buffer.title.c_str() : path.c_str(), OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY, L"All files (*.*)|*.*||", this);
            if (dialog.DoModal() != IDOK)
                return false;
            path = std::wstring(dialog.GetPathName());
        }
        bool lossy = false;
        EncodeText(buffer.GetAllText(), buffer.encoding, &lossy);
        if (lossy)
        {
            CString question;
            question.Format(L"Some characters cannot be represented in %s and will be replaced.\nSave anyway?", EncodingName(buffer.encoding).c_str());
            if (AfxMessageBox(question, MB_YESNO | MB_ICONEXCLAMATION) != IDYES)
                return false;
        }
        std::wstring error;
        if (!m_doc.SaveFile(pane, path, error))
        {
            CString message;
            message.Format(L"Cannot save %s:\n%s", path.c_str(), error.c_str());
            AfxMessageBox(message, MB_ICONERROR);
            return false;
        }
        Pane(pane)->Refresh();
        Invalidate(FALSE);
        NotifyFrame();
        if (onSaved)
            onSaved();
        return true;
    }

    // ---- settings ----

    void CompareWnd::SetCompareOptions(const CompareOptions& options)
    {
        m_doc.SetOptions(options);
        OnTextChanged();
    }

    void CompareWnd::SetViewSettings(const ViewSettings& settings)
    {
        m_view = settings;
        m_rowsRevision = 0;
        Layout();
        for (auto& pane : m_panes)
            pane->Refresh();
        if (!m_panes.empty())
            SyncScroll(m_active);
    }

    const RowLayout& CompareWnd::Rows()
    {
        const int count = PaneCount();
        int wrap = 0;
        if (m_view.wordWrap)
        {
            // One wrap width for all panes, so equal lines wrap the same way.
            wrap = INT_MAX;
            for (auto& pane : m_panes)
            {
                if (pane->m_hWnd)
                    wrap = (std::min)(wrap, pane->TextAreaColumns());
            }
            if (wrap == INT_MAX)
                wrap = 80;
        }
        const int key[5] = { wrap, m_view.verticalPadding ? 1 : 0, m_view.tabSize, count, static_cast<int>(m_doc.Chunks().size()) };
        if (m_rowsRevision == m_doc.Revision() && std::equal(key, key + 5, m_rowsKey) && !m_rows.lineRow.empty())
            return m_rows;
        m_rowsRevision = m_doc.Revision();
        std::copy(key, key + 5, m_rowsKey);

        m_rows.wrapColumns = wrap;
        m_rows.lineRow.assign(static_cast<size_t>(count), std::vector<int>());
        m_rows.ownRows.assign(static_cast<size_t>(count), std::vector<int>());
        m_rows.chunkRow.assign(static_cast<size_t>(count), std::vector<int>());
        std::vector<int> cols;
        for (int pane = 0; pane < count; ++pane)
        {
            const TextBuffer& buffer = m_doc.Buffer(pane);
            std::vector<int>& own = m_rows.ownRows[static_cast<size_t>(pane)];
            own.assign(static_cast<size_t>(buffer.LineCount()), 1);
            if (wrap > 0)
            {
                for (int line = 0; line < buffer.LineCount(); ++line)
                {
                    BuildColumns(buffer.LineText(line), m_view.tabSize, cols);
                    own[static_cast<size_t>(line)] = (std::max)(1, (cols.back() + wrap - 1) / wrap);
                }
            }
            m_rows.lineRow[static_cast<size_t>(pane)].assign(static_cast<size_t>(buffer.LineCount()) + 1, 0);
        }
        const std::vector<Chunk>& chunks = m_doc.Chunks();
        int row[3] = {};
        for (const Chunk& chunk : chunks)
        {
            int height = 0;
            for (int pane = 0; pane < count; ++pane)
            {
                m_rows.chunkRow[static_cast<size_t>(pane)].push_back(row[pane]);
                const int start = row[pane];
                for (int line = chunk.start[pane]; line < chunk.End(pane); ++line)
                {
                    m_rows.lineRow[static_cast<size_t>(pane)][static_cast<size_t>(line)] = row[pane];
                    row[pane] += m_rows.ownRows[static_cast<size_t>(pane)][static_cast<size_t>(line)];
                }
                height = (std::max)(height, row[pane] - start);
            }
            if (m_view.verticalPadding)
            {
                // Blank rows at the end of the shorter sides keep the blocks side by side.
                for (int pane = 0; pane < count; ++pane)
                    row[pane] = m_rows.chunkRow[static_cast<size_t>(pane)].back() + height;
            }
        }
        for (int pane = 0; pane < count; ++pane)
        {
            m_rows.chunkRow[static_cast<size_t>(pane)].push_back(row[pane]);
            m_rows.lineRow[static_cast<size_t>(pane)].back() = row[pane];
        }
        return m_rows;
    }

    int CompareWnd::CorrespondingLine(int from, int line, int to)
    {
        if (from == to)
            return line;
        const int index = m_doc.ChunkAt(from, line);
        if (index < 0)
            return -1;
        const Chunk& chunk = m_doc.Chunks()[static_cast<size_t>(index)];
        const int rel = line - chunk.start[from];
        if (chunk.IsDifference() && chunk.count[from] > 0 && chunk.count[to] > 0)
        {
            const ChunkDetail& mine = Detail(index, from);
            if (mine.partner == to && rel >= 0 && rel < static_cast<int>(mine.partnerLine.size()))
                return mine.partnerLine[static_cast<size_t>(rel)] < 0 ? -1 : chunk.start[to] + mine.partnerLine[static_cast<size_t>(rel)];
            const ChunkDetail& theirs = Detail(index, to);
            if (theirs.partner == from)
            {
                for (size_t k = 0; k < theirs.partnerLine.size(); ++k)
                {
                    if (theirs.partnerLine[k] == rel)
                        return chunk.start[to] + static_cast<int>(k);
                }
                return -1;
            }
        }
        return rel >= 0 && rel < chunk.count[to] ? chunk.start[to] + rel : -1;
    }

    void CompareWnd::UpdateTextDrag(int, CPoint screenPoint, bool)
    {
        const int target = PaneFromScreenPoint(screenPoint);
        for (int pane = 0; pane < PaneCount(); ++pane)
            Pane(pane)->SetDropPos(pane == target ? Pane(pane)->PosFromScreen(screenPoint) : TextPos{ -1, 0 });
        SetCursor(::LoadCursor(nullptr, target >= 0 && !m_doc.Buffer(target).readOnly ? IDC_ARROW : IDC_NO));
    }

    void CompareWnd::EndTextDrag(int sourcePane, CPoint screenPoint, bool copy, bool drop)
    {
        for (int pane = 0; pane < PaneCount(); ++pane)
            Pane(pane)->SetDropPos({ -1, 0 });
        if (!drop)
            return;
        const int target = PaneFromScreenPoint(screenPoint);
        if (target < 0)
            return;
        PaneView* source = Pane(sourcePane);
        TextPos from, to;
        source->GetSelection(from, to);
        if (from == to)
            return;
        TextPos pos = Pane(target)->PosFromScreen(screenPoint);
        if (target == sourcePane && from <= pos && pos <= to)
        {
            source->SetCaret(pos, false);  // dropped onto itself: just a click
            return;
        }
        if (m_doc.Buffer(target).readOnly || (!copy && m_doc.Buffer(sourcePane).readOnly))
        {
            MessageBeep(MB_ICONEXCLAMATION);
            return;
        }
        const std::wstring text = m_doc.Buffer(sourcePane).GetText(from, to);
        m_doc.BeginGroup(copy ? target : sourcePane, copy ? pos : from);
        if (!copy)
        {
            m_doc.ReplaceRange(sourcePane, from, to, L"");
            // Removing the text moves a drop point after it in the same pane.
            if (target == sourcePane && to <= pos)
            {
                if (pos.line == to.line)
                    pos = { from.line, from.col + (pos.col - to.col) };
                else
                    pos.line -= to.line - from.line;
            }
        }
        const TextPos end = m_doc.ReplaceRange(target, pos, pos, text);
        m_doc.EndGroup();
        OnTextChanged();
        if (target != sourcePane)
            source->SetCaret(from, false, false);
        Pane(target)->SetFocus();
        Pane(target)->SetSelection(pos, end);
    }

    void CompareWnd::ApplySettings()
    {
        m_doc.SetOptions(App().compareOptions);
        SetViewSettings(App().viewSettings);
        OnTextChanged();
    }

    CString CompareWnd::StatusText(int part) const
    {
        return part == 0 ? EncodingStatus() : (part == 1 ? ChangesStatus() : PositionStatus());
    }

    void CompareWnd::UpdateMetrics()
    {
        for (auto& pane : m_panes)
            pane->UpdateMetrics();
        Layout();
    }

    // ---- coordination of the child windows ----

    void CompareWnd::SetActivePane(int pane)
    {
        if (pane == m_active)
            return;
        m_active = pane;
        Invalidate(FALSE);
        InvalidateAll();
        NotifyFrame();
    }

    void CompareWnd::SyncScroll(int fromPane)
    {
        if (m_syncing)
            return;
        m_syncing = true;
        PaneView* from = Pane(fromPane);
        const RowLayout& rows = Rows();
        if (m_view.verticalPadding)
        {
            // Padded rows line up exactly: every pane shows the same rows.
            for (int pane = 0; pane < PaneCount(); ++pane)
            {
                if (pane != fromPane)
                    Pane(pane)->SetTopRow(from->TopRow(), false);
            }
        }
        else
        {
            // Keep the lines at the vertical centre of each pane corresponding.
            const double centerRow = from->TopRow() + from->VisibleRows() / 2.0;
            const std::vector<int>& fromRows = rows.lineRow[static_cast<size_t>(fromPane)];
            const int fromLines = static_cast<int>(fromRows.size()) - 1;
            auto it = std::upper_bound(fromRows.begin(), fromRows.begin() + fromLines, static_cast<int>(centerRow));
            const int line = (std::max)(0, static_cast<int>(it - fromRows.begin()) - 1);
            double centerLine = line;
            if (line < fromLines)
            {
                const int own = rows.ownRows[static_cast<size_t>(fromPane)][static_cast<size_t>(line)];
                centerLine += (std::min)(1.0, (centerRow - fromRows[static_cast<size_t>(line)]) / own);
            }
            for (int pane = 0; pane < PaneCount(); ++pane)
            {
                if (pane == fromPane)
                    continue;
                PaneView* other = Pane(pane);
                const double mapped = m_doc.MapLine(fromPane, centerLine, pane);
                const std::vector<int>& otherRows = rows.lineRow[static_cast<size_t>(pane)];
                const int otherLines = static_cast<int>(otherRows.size()) - 1;
                const int target = (std::max)(0, (std::min)(static_cast<int>(mapped), otherLines));
                double row = otherRows[static_cast<size_t>(target)];
                if (target < otherLines)
                    row += (mapped - target) * rows.ownRows[static_cast<size_t>(pane)][static_cast<size_t>(target)];
                other->SetTopRow(static_cast<int>(row - other->VisibleRows() / 2.0 + 0.5), false);
            }
        }
        m_syncing = false;
        InvalidateAll();
    }

    void CompareWnd::InvalidateAll()
    {
        for (auto& pane : m_panes)
            pane->Invalidate(FALSE);
        for (auto& gutter : m_gutters)
            gutter->Invalidate(FALSE);
        if (m_overviewLeft)
            m_overviewLeft->Invalidate(FALSE);
        if (m_overviewRight)
            m_overviewRight->Invalidate(FALSE);
        if (m_lineDetail && m_lineDetail->IsWindowVisible())
            m_lineDetail->Invalidate(FALSE);
    }

    void CompareWnd::OnTextChanged()
    {
        for (auto& pane : m_panes)
            pane->Refresh();
        InvalidateAll();
        Invalidate(FALSE);
        NotifyFrame();
    }

    void CompareWnd::OnCaretMoved(int pane)
    {
        InvalidateAll();
        NotifyFrame();
        // A bookmark comment on the caret's line is shown in the status bar.
        if (PaneView* view = Pane(pane))
        {
            const int line = view->Caret().line;
            const std::wstring& comment = m_doc.Buffer(pane).GetLine(line).comment;
            if (line != m_commentLine || pane != m_commentPane)
            {
                m_commentLine = line;
                m_commentPane = pane;
                if (CFrameWnd* frame = GetParentFrame(); frame && (!comment.empty() || m_commentShown))
                {
                    std::wstring text = comment;
                    std::replace(text.begin(), text.end(), L'\n', L' ');
                    frame->SetMessageText(comment.empty() ? std::wstring(L"Ready").c_str() : (L"\x270E Bookmark comment: " + text).c_str());
                    m_commentShown = !comment.empty();
                }
            }
        }
    }

    void CompareWnd::NotifyFrame()
    {
        if (CFrameWnd* frame = GetParentFrame())
            frame->PostMessage(WM_MF_STATECHANGED);
    }

    const ChunkDetail& CompareWnd::Detail(int chunkIndex, int pane)
    {
        if (m_detailsRevision != m_doc.Revision())
        {
            m_details.clear();
            m_detailsRevision = m_doc.Revision();
        }
        const auto key = std::make_pair(chunkIndex, pane);
        auto found = m_details.find(key);
        if (found != m_details.end())
            return found->second;

        ChunkDetail detail;
        const Chunk& chunk = m_doc.Chunks()[static_cast<size_t>(chunkIndex)];
        if (!m_doc.IsThreeWay())
            detail.partner = 1 - pane;
        else if (pane == 1)
            detail.partner = chunk.kind == ChunkKind::RightOnly ? 2 : 0;
        else
            detail.partner = 1;
        std::vector<const std::wstring*> mine, theirs;
        for (int line = chunk.start[pane]; line < chunk.End(pane); ++line)
            mine.push_back(&m_doc.Buffer(pane).LineText(line));
        for (int line = chunk.start[detail.partner]; line < chunk.End(detail.partner); ++line)
            theirs.push_back(&m_doc.Buffer(detail.partner).LineText(line));
        detail.paired.assign(mine.size(), 0);
        detail.ranges.resize(mine.size());
        detail.partnerLine.assign(mine.size(), -1);
        for (const auto& pair : PairLines(mine, theirs, m_doc.Options()))
        {
            detail.paired[static_cast<size_t>(pair.first)] = 1;
            detail.partnerLine[static_cast<size_t>(pair.first)] = pair.second;
            detail.ranges[static_cast<size_t>(pair.first)] = DiffLineChars(*mine[static_cast<size_t>(pair.first)], *theirs[static_cast<size_t>(pair.second)], m_doc.Options()).a;
        }
        return m_details.emplace(key, std::move(detail)).first->second;
    }

    int CompareWnd::CurrentChunk() const
    {
        PaneView* pane = Pane(m_active);
        if (!pane)
            return -1;
        const int index = m_doc.ChunkAt(m_active, pane->Caret().line);
        if (index < 0 || !m_doc.Chunks()[static_cast<size_t>(index)].IsDifference())
            return -1;
        return index;
    }

    void CompareWnd::CopyChunk(int chunkIndex, int from, int to)
    {
        if (to < 0 || from == to)
            return;
        if (m_doc.Buffer(to).readOnly)
        {
            MessageBeep(MB_ICONEXCLAMATION);
            return;
        }
        const int line = m_doc.Chunks()[static_cast<size_t>(chunkIndex)].start[to];
        m_doc.CopyChunk(chunkIndex, from, to);
        Pane(to)->SetCaret({ line, 0 }, false, false);
        OnTextChanged();
    }

    void CompareWnd::JumpToLine(int pane, int line)
    {
        PaneView* view = Pane(pane);
        const int row = view->TopRow() + view->LineToY(line) / view->LineHeight();
        view->SetTopRow(row - view->VisibleRows() / 2, true);
    }

    void CompareWnd::GoToChunk(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_doc.Chunks().size()))
            return;
        const Chunk chunk = m_doc.Chunks()[static_cast<size_t>(index)];
        for (int pane = 0; pane < PaneCount(); ++pane)
        {
            if (pane != m_active)
                Pane(pane)->SetCaret({ chunk.start[pane], 0 }, false, false);
        }
        PaneView* active = Pane(m_active);
        active->SetCaret({ chunk.start[m_active], 0 }, false, false);
        active->CenterLine(chunk.start[m_active], true);
        InvalidateAll();
    }

    bool CompareWnd::CanNavigate(bool forward, bool conflictsOnly) const
    {
        PaneView* pane = Pane(m_active);
        return pane && m_doc.FindDifference(m_active, pane->Caret().line, forward, conflictsOnly) >= 0;
    }

    void CompareWnd::Navigate(bool forward, bool conflictsOnly)
    {
        PaneView* pane = Pane(m_active);
        const int index = m_doc.FindDifference(m_active, pane->Caret().line, forward, conflictsOnly);
        if (index >= 0)
            GoToChunk(index);
        else
            MessageBeep(MB_OK);
    }

    void CompareWnd::ShowPaneMenu(int pane, CPoint screenPoint)
    {
        if (Pane(pane) != GetFocus())
            Pane(pane)->SetFocus();
        SetActivePane(pane);
        CMenu menu;
        if (!menu.LoadMenuW(IDR_POPUPS))
            return;
        if (CMenu* popup = menu.GetSubMenu(POPUP_PANE))
            popup->TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, GetParentFrame());
    }

    int CompareWnd::PaneFromScreenPoint(CPoint point) const
    {
        for (int pane = 0; pane < PaneCount(); ++pane)
        {
            CRect rect;
            m_panes[static_cast<size_t>(pane)]->GetWindowRect(&rect);
            CRect bar = PathBarRect(pane);
            ClientToScreen(&bar);
            if (rect.PtInRect(point) || bar.PtInRect(point))
                return pane;
        }
        return -1;
    }

    // ---- status bar ----

    CString CompareWnd::EncodingStatus() const
    {
        const TextBuffer& buffer = m_doc.Buffer(m_active);
        CString text = EncodingName(buffer.encoding).c_str();
        text += L" \x2022 ";
        text += HasMixedEol(buffer.GetLines()) ? L"Mixed line endings" : EolName(DominantEol(buffer.GetLines(), buffer.eol));
        if (buffer.readOnly)
            text += L" \x2022 Read-only";
        return text;
    }

    CString CompareWnd::ChangesStatus() const
    {
        const ComparisonStats stats = m_doc.Stats();
        if (stats.Differences() == 0)
            return L"No differences";
        CString text = Plural(stats.removals, L"removal", L"removals") + L" \x2022 " + Plural(stats.insertions, L"insertion", L"insertions") +
            L" \x2022 " + Plural(stats.changes, L"change", L"changes");
        if (m_doc.IsThreeWay())
            text += L" \x2022 " + Plural(stats.conflicts, L"conflict", L"conflicts");
        return text;
    }

    CString CompareWnd::PositionStatus() const
    {
        PaneView* pane = Pane(m_active);
        if (!pane)
            return L"";
        const TextPos caret = pane->Caret();
        const std::wstring& line = m_doc.Buffer(m_active).LineText(caret.line);
        int column = 0;
        for (int i = 0; i < caret.col && i < static_cast<int>(line.size()); ++i)
            column += line[static_cast<size_t>(i)] == L'\t' ? m_view.tabSize - column % m_view.tabSize : 1;
        CString text;
        text.Format(L"Line %d of %d \x2022 Column %d", caret.line + 1, m_doc.Buffer(m_active).LineCount(), column + 1);
        return text;
    }

    // ---- find / replace ----

    bool CompareWnd::FindNext(const std::wstring& what, SearchOptions options, bool)
    {
        PaneView* pane = Pane(m_active);
        TextPos from, to;
        pane->GetSelection(from, to);
        TextPos start, end;
        if (!FindInBuffer(m_doc.Buffer(m_active), what, options.backward ? from : to, options, start, end))
            return false;
        const bool visible = start.line >= pane->TopLine() && start.line < pane->BottomLine();
        pane->SetSelection(start, end, visible);
        if (!visible)
            pane->CenterLine(start.line, true);
        pane->EnsureCaretVisible();
        return true;
    }

    bool CompareWnd::ReplaceCurrent(const std::wstring& what, const std::wstring& with, SearchOptions options, bool selectionOnly)
    {
        PaneView* pane = Pane(m_active);
        if (!pane->CanEdit())
        {
            MessageBeep(MB_ICONEXCLAMATION);
            return false;
        }
        const std::wstring selected = pane->SelectedText();
        const bool matches = selected.size() == what.size() && (options.matchCase ? selected == what : _wcsicmp(selected.c_str(), what.c_str()) == 0);
        if (matches)
            pane->InsertText(with);
        options.backward = false;
        return FindNext(what, options, selectionOnly) || matches;
    }

    int CompareWnd::ReplaceAll(const std::wstring& what, const std::wstring& with, SearchOptions options, bool selectionOnly)
    {
        PaneView* pane = Pane(m_active);
        if (!pane->CanEdit() || what.empty())
            return 0;
        const TextBuffer& buffer = m_doc.Buffer(m_active);
        TextPos rangeStart, rangeEnd;
        if (selectionOnly && pane->HasSelection())
            pane->GetSelection(rangeStart, rangeEnd);
        else
            rangeEnd = buffer.EndPos();
        options.backward = false;
        options.wrap = false;
        int count = 0;
        TextPos from = rangeStart;
        TextPos start, end;
        m_doc.BeginGroup(m_active, rangeStart);
        while (FindInBuffer(buffer, what, from, options, start, end, rangeStart, rangeEnd) && rangeStart != rangeEnd)
        {
            const TextPos after = m_doc.ReplaceRange(m_active, start, end, with);
            if (rangeEnd.line == start.line)
                rangeEnd.col += static_cast<int>(with.size()) - static_cast<int>(what.size());
            from = after;
            ++count;
            if (count > 1000000)
                break;
        }
        m_doc.EndGroup();
        if (count)
        {
            pane->SetCaret(pane->Caret(), false, false);
            OnTextChanged();
        }
        return count;
    }

    // ---- commands ----

    void CompareWnd::OnUpdateAlways(CCmdUI* ui)
    {
        ui->Enable(TRUE);
    }

    void CompareWnd::OnSavePane()
    {
        SavePane(m_active, false);
    }

    void CompareWnd::OnSavePaneAs()
    {
        SavePane(m_active, true);
    }

    void CompareWnd::OnSaveAll()
    {
        for (int pane = 0; pane < PaneCount(); ++pane)
        {
            if (m_doc.IsModified(pane) && !SavePane(pane, false))
                return;
        }
    }

    void CompareWnd::OnUpdateSavePane(CCmdUI* ui)
    {
        ui->Enable(m_doc.IsModified(m_active) || m_doc.Buffer(m_active).path.empty());
    }

    void CompareWnd::OnUpdateSaveAll(CCmdUI* ui)
    {
        ui->Enable(m_doc.AnyModified());
    }

    void CompareWnd::OnOpenInPane()
    {
        if (m_doc.IsModified(m_active))
        {
            if (AfxMessageBox(L"Discard the changes in this pane and open another file?", MB_YESNO | MB_ICONQUESTION) != IDYES)
                return;
        }
        CFileDialog dialog(TRUE, nullptr, m_doc.Buffer(m_active).path.c_str(), OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, L"All files (*.*)|*.*||", this);
        if (dialog.DoModal() != IDOK)
            return;
        CString error;
        if (!LoadIntoPane(m_active, std::wstring(dialog.GetPathName()), error))
            AfxMessageBox(error, MB_ICONERROR);
    }

    void CompareWnd::AfterUndoRedo(bool ok, int pane, TextPos caret)
    {
        if (!ok)
            return;
        OnTextChanged();
        m_panes[static_cast<size_t>(pane)]->SetFocus();
        SetActivePane(pane);
        Pane(pane)->SetCaret(caret, false, true);
    }

    void CompareWnd::OnUndo()
    {
        int pane = m_active;
        TextPos caret;
        const bool ok = m_doc.Undo(pane, caret);
        AfterUndoRedo(ok, pane, caret);
    }

    void CompareWnd::OnRedo()
    {
        int pane = m_active;
        TextPos caret;
        const bool ok = m_doc.Redo(pane, caret);
        AfterUndoRedo(ok, pane, caret);
    }

    void CompareWnd::OnUpdateUndo(CCmdUI* ui)
    {
        ui->Enable(m_doc.CanUndo());
    }

    void CompareWnd::OnUpdateRedo(CCmdUI* ui)
    {
        ui->Enable(m_doc.CanRedo());
    }

    void CompareWnd::OnCut()
    {
        Pane(m_active)->Cut();
    }

    void CompareWnd::OnCopy()
    {
        Pane(m_active)->Copy();
    }

    void CompareWnd::OnPaste()
    {
        Pane(m_active)->Paste();
    }

    void CompareWnd::OnSelectAll()
    {
        Pane(m_active)->SelectAll();
    }

    void CompareWnd::OnUpdateCut(CCmdUI* ui)
    {
        ui->Enable(Pane(m_active)->CanEdit());
    }

    void CompareWnd::OnUpdateCopy(CCmdUI* ui)
    {
        ui->Enable(TRUE);
    }

    void CompareWnd::OnUpdatePaste(CCmdUI* ui)
    {
        ui->Enable(Pane(m_active)->CanEdit() && IsClipboardFormatAvailable(CF_UNICODETEXT));
    }

    void CompareWnd::OnUpdateEditable(CCmdUI* ui)
    {
        ui->Enable(Pane(m_active)->CanEdit());
    }

    void CompareWnd::OnGoToLine()
    {
        PaneView* pane = Pane(m_active);
        GoToLineDlg dialog(pane->Caret().line + 1, m_doc.Buffer(m_active).LineCount(), this);
        if (dialog.DoModal() != IDOK)
            return;
        pane->SetCaret({ dialog.line - 1, 0 }, false, false);
        pane->CenterLine(dialog.line - 1, true);
        pane->SetFocus();
    }

    void CompareWnd::OnIndent()
    {
        Pane(m_active)->IndentSelection(false);
    }

    void CompareWnd::OnOutdent()
    {
        Pane(m_active)->IndentSelection(true);
    }

    void CompareWnd::OnStart()
    {
        if (m_doc.AnyModified() && AfxMessageBox(L"Reload the files from disk and discard unsaved changes?", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
        for (int pane = 0; pane < PaneCount(); ++pane)
        {
            const std::wstring path = m_doc.Buffer(pane).path;
            CString error;
            if (!path.empty() && !LoadIntoPane(pane, path, error))
                AfxMessageBox(error, MB_ICONERROR);
        }
    }

    void CompareWnd::OnUpdateStart(CCmdUI* ui)
    {
        bool hasFile = false;
        for (int pane = 0; pane < PaneCount(); ++pane)
            hasFile = hasFile || !m_doc.Buffer(pane).path.empty();
        ui->Enable(hasFile);
    }

    void CompareWnd::OnUpdateStop(CCmdUI* ui)
    {
        // File comparisons finish synchronously; Stop is for long folder comparisons (later phase).
        ui->Enable(FALSE);
    }

    void CompareWnd::OnRecompare()
    {
        m_doc.Recompare();
        OnTextChanged();
    }

    void CompareWnd::OnLayoutTwo()
    {
        if (!m_doc.IsThreeWay())
            return;
        if (m_doc.IsModified(1) && AfxMessageBox(L"Close the middle file and discard its unsaved changes?", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
        m_doc.SetPaneCount(2);
        m_active = 0;
        CreateChildren();
        OnTextChanged();
    }

    void CompareWnd::OnLayoutThree()
    {
        if (m_doc.IsThreeWay())
            return;
        m_doc.SetPaneCount(3);
        if (m_active == 1)
            m_active = 2;
        CreateChildren();
        OnTextChanged();
    }

    void CompareWnd::OnUpdateLayoutTwo(CCmdUI* ui)
    {
        ui->Enable(TRUE);
        ui->SetRadio(!m_doc.IsThreeWay());
    }

    void CompareWnd::OnUpdateLayoutThree(CCmdUI* ui)
    {
        ui->Enable(TRUE);
        ui->SetRadio(m_doc.IsThreeWay());
    }

    void CompareWnd::OnStatistics()
    {
        const ComparisonStats stats = m_doc.Stats();
        CString text;
        int differentLines[3] = {};
        for (const Chunk& chunk : m_doc.Chunks())
        {
            if (!chunk.IsDifference())
                continue;
            for (int pane = 0; pane < PaneCount(); ++pane)
            {
                if (m_doc.PaneDiffersInChunk(chunk, pane))
                    differentLines[pane] += chunk.count[pane];
            }
        }
        static const wchar_t* twoWayNames[] = { L"Left", L"Right" };
        static const wchar_t* threeWayNames[] = { L"Left", L"Ancestor (merge)", L"Right" };
        for (int pane = 0; pane < PaneCount(); ++pane)
        {
            const TextBuffer& buffer = m_doc.Buffer(pane);
            CString line;
            line.Format(L"%s: %s\r\n    %d lines, %d in changed blocks; %s, %s%s\r\n\r\n", m_doc.IsThreeWay() ? threeWayNames[pane] : twoWayNames[pane],
                buffer.path.empty() ? buffer.title.c_str() : buffer.path.c_str(), buffer.LineCount(), differentLines[pane],
                EncodingName(buffer.encoding).c_str(), EolName(DominantEol(buffer.GetLines(), buffer.eol)), m_doc.IsModified(pane) ? L", modified" : L"");
            text += line;
        }
        CString summary;
        summary.Format(L"Changed blocks: %d\r\n    %d insertions, %d removals, %d changes", stats.Differences(), stats.insertions, stats.removals, stats.changes);
        text += summary;
        if (m_doc.IsThreeWay())
        {
            summary.Format(L", %d conflicts", stats.conflicts);
            text += summary;
        }
        const CompareOptions& options = m_doc.Options();
        text += L"\r\n\r\nOptions: ";
        text += options.whitespace == WhitespaceMode::Compare ? L"whitespace compared" : (options.whitespace == WhitespaceMode::IgnoreChanges ? L"whitespace changes ignored" : L"all whitespace ignored");
        text += options.ignoreCase ? L", case ignored" : L", case compared";
        text += options.ignoreEol ? L", line endings ignored" : L", line endings compared";
        TextDlg dialog(IDD_STATISTICS, text, this);
        dialog.DoModal();
    }

    void CompareWnd::OnNextChange()
    {
        Navigate(true, false);
    }

    void CompareWnd::OnPrevChange()
    {
        Navigate(false, false);
    }

    void CompareWnd::OnNextConflict()
    {
        Navigate(true, true);
    }

    void CompareWnd::OnPrevConflict()
    {
        Navigate(false, true);
    }

    void CompareWnd::OnUpdateNextChange(CCmdUI* ui)
    {
        ui->Enable(CanNavigate(true, false));
    }

    void CompareWnd::OnUpdatePrevChange(CCmdUI* ui)
    {
        ui->Enable(CanNavigate(false, false));
    }

    void CompareWnd::OnUpdateNextConflict(CCmdUI* ui)
    {
        ui->Enable(m_doc.IsThreeWay() && CanNavigate(true, true));
    }

    void CompareWnd::OnUpdatePrevConflict(CCmdUI* ui)
    {
        ui->Enable(m_doc.IsThreeWay() && CanNavigate(false, true));
    }

    bool CompareWnd::CopyPanes(bool toRight, int& from, int& to) const
    {
        const int chunk = CurrentChunk();
        if (chunk < 0)
            return false;
        if (m_doc.IsThreeWay())
        {
            from = toRight ? 0 : 2;
            to = 1;
        }
        else
        {
            from = toRight ? 0 : 1;
            to = toRight ? 1 : 0;
        }
        const Chunk& current = m_doc.Chunks()[static_cast<size_t>(chunk)];
        return m_doc.PaneDiffersInChunk(current, from) && m_doc.PaneDiffersInChunk(current, to) && !m_doc.Buffer(to).readOnly;
    }

    void CompareWnd::OnCopyToRight()
    {
        int from, to;
        if (CopyPanes(true, from, to))
            CopyChunk(CurrentChunk(), from, to);
    }

    void CompareWnd::OnCopyToLeft()
    {
        int from, to;
        if (CopyPanes(false, from, to))
            CopyChunk(CurrentChunk(), from, to);
    }

    void CompareWnd::OnUpdateCopyToRight(CCmdUI* ui)
    {
        int from, to;
        ui->Enable(CopyPanes(true, from, to));
    }

    void CompareWnd::OnUpdateCopyToLeft(CCmdUI* ui)
    {
        int from, to;
        ui->Enable(CopyPanes(false, from, to));
    }

    void CompareWnd::OnAutoMerge()
    {
        if (m_doc.Buffer(1).readOnly)
        {
            AfxMessageBox(L"The middle file is read-only.", MB_ICONEXCLAMATION);
            return;
        }
        int conflicts = 0;
        const int merged = m_doc.AutoMerge(conflicts);
        OnTextChanged();
        CString message;
        if (merged == 0 && conflicts == 0)
            message = L"There is nothing to merge.";
        else
            message.Format(L"Merged %d change%s into the middle file. %d conflict%s left for manual merging.", merged, merged == 1 ? L"" : L"s", conflicts, conflicts == 1 ? L" is" : L"s are");
        if (conflicts > 0)
        {
            const int first = m_doc.FindDifference(1, -1, true, true);
            Pane(1)->SetFocus();
            SetActivePane(1);
            GoToChunk(first);
        }
        if (CFrameWnd* frame = GetParentFrame())
            frame->SetMessageText(message);
    }

    void CompareWnd::OnUpdateAutoMerge(CCmdUI* ui)
    {
        bool mergeable = false;
        if (m_doc.IsThreeWay())
        {
            for (const Chunk& chunk : m_doc.Chunks())
            {
                if (chunk.kind == ChunkKind::LeftOnly || chunk.kind == ChunkKind::RightOnly || chunk.kind == ChunkKind::BothSame)
                {
                    mergeable = true;
                    break;
                }
            }
        }
        ui->Enable(mergeable);
    }

    void CompareWnd::OnMarkResolved()
    {
        int first, last;
        Pane(m_active)->SelectedLines(first, last);
        m_doc.SetResolved(m_active, first, last, true);
        InvalidateAll();
    }

    void CompareWnd::OnUnmarkResolved()
    {
        int first, last;
        Pane(m_active)->SelectedLines(first, last);
        m_doc.SetResolved(m_active, first, last, false);
        InvalidateAll();
    }

    void CompareWnd::OnReplaceRight()
    {
        // Two-way: the right file; three-way: the middle file.
        if (m_doc.Buffer(1).readOnly)
            return;
        m_doc.ReplaceAll(0, 1);
        OnTextChanged();
    }

    void CompareWnd::OnReplaceLeft()
    {
        const int from = m_doc.IsThreeWay() ? 2 : 1;
        const int to = m_doc.IsThreeWay() ? 1 : 0;
        if (m_doc.Buffer(to).readOnly)
            return;
        m_doc.ReplaceAll(from, to);
        OnTextChanged();
    }

    void CompareWnd::OnUpdateReplaceRight(CCmdUI* ui)
    {
        ui->Enable(!m_doc.Buffer(1).readOnly);
        ui->SetText(m_doc.IsThreeWay() ? L"Replace Middle File with Left File" : L"Replace Right File with Left File");
    }

    void CompareWnd::OnUpdateReplaceLeft(CCmdUI* ui)
    {
        ui->Enable(!m_doc.Buffer(m_doc.IsThreeWay() ? 1 : 0).readOnly);
        ui->SetText(m_doc.IsThreeWay() ? L"Replace Middle File with Right File" : L"Replace Left File with Right File");
    }

    namespace
    {
        TextEncoding EncodingForCommand(UINT id)
        {
            switch (id)
            {
            case ID_ENC_UTF8_BOM:
                return { EncodingType::Utf8, true, 0 };
            case ID_ENC_UTF16LE_BOM:
                return { EncodingType::Utf16LE, true, 0 };
            case ID_ENC_UTF16BE_BOM:
                return { EncodingType::Utf16BE, true, 0 };
            case ID_ENC_UTF16LE:
                return { EncodingType::Utf16LE, false, 0 };
            case ID_ENC_UTF16BE:
                return { EncodingType::Utf16BE, false, 0 };
            case ID_ENC_ANSI:
                return { EncodingType::CodePage, false, 0 };
            default:
                return { EncodingType::Utf8, false, 0 };
            }
        }
    }

    void CompareWnd::OnEncoding(UINT id)
    {
        m_doc.SetEncoding(m_active, EncodingForCommand(id));
        Invalidate(FALSE);
        NotifyFrame();
    }

    void CompareWnd::OnUpdateEncoding(CCmdUI* ui)
    {
        const TextEncoding& current = m_doc.Buffer(m_active).encoding;
        const TextEncoding option = EncodingForCommand(ui->m_nID);
        ui->Enable(TRUE);
        ui->SetRadio(current.type == option.type && current.bom == option.bom && (current.type != EncodingType::CodePage || current.codePage == 0));
    }

    void CompareWnd::OnReopen(UINT id)
    {
        const std::wstring path = m_doc.Buffer(m_active).path;
        if (path.empty())
            return;
        if (m_doc.IsModified(m_active) && AfxMessageBox(L"Reload the file and discard its unsaved changes?", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return;
        TextEncoding forced;
        const TextEncoding* forcedPtr = &forced;
        switch (id)
        {
        case ID_REOPEN_UTF8:
            forced = { EncodingType::Utf8, false, 0 };
            break;
        case ID_REOPEN_UTF16LE:
            forced = { EncodingType::Utf16LE, false, 0 };
            break;
        case ID_REOPEN_ANSI:
            forced = { EncodingType::CodePage, false, 0 };
            break;
        case ID_REOPEN_SHIFT_JIS:
            forced = { EncodingType::CodePage, false, 932 };
            break;
        case ID_REOPEN_EUC_JP:
            forced = { EncodingType::CodePage, false, 20932 };
            break;
        default:
            forcedPtr = nullptr;
            break;
        }
        CString error;
        if (!LoadIntoPane(m_active, path, error, forcedPtr))
            AfxMessageBox(error, MB_ICONERROR);
    }

    void CompareWnd::OnUpdateReopen(CCmdUI* ui)
    {
        ui->Enable(!m_doc.Buffer(m_active).path.empty());
    }

    void CompareWnd::OnEol(UINT id)
    {
        if (m_doc.Buffer(m_active).readOnly)
            return;
        const Eol eol = id == ID_EOL_LF ? Eol::Lf : (id == ID_EOL_CR ? Eol::Cr : Eol::CrLf);
        m_doc.ConvertLineEndings(m_active, eol);
        OnTextChanged();
    }

    void CompareWnd::OnUpdateEol(CCmdUI* ui)
    {
        const TextBuffer& buffer = m_doc.Buffer(m_active);
        const Eol eol = ui->m_nID == ID_EOL_LF ? Eol::Lf : (ui->m_nID == ID_EOL_CR ? Eol::Cr : Eol::CrLf);
        ui->Enable(!buffer.readOnly);
        ui->SetRadio(!HasMixedEol(buffer.GetLines()) && DominantEol(buffer.GetLines(), buffer.eol) == eol);
    }

    void CompareWnd::OnToggleBookmark()
    {
        m_doc.ToggleBookmark(m_active, Pane(m_active)->Caret().line);
        m_commentLine = -1;
        OnCaretMoved(m_active);
    }

    void CompareWnd::OnNextBookmark()
    {
        PaneView* pane = Pane(m_active);
        const int line = m_doc.FindBookmark(m_active, pane->Caret().line, true);
        if (line < 0)
        {
            MessageBeep(MB_OK);
            return;
        }
        pane->SetCaret({ line, 0 }, false, false);
        pane->CenterLine(line, true);
    }

    void CompareWnd::OnPrevBookmark()
    {
        PaneView* pane = Pane(m_active);
        const int line = m_doc.FindBookmark(m_active, pane->Caret().line, false);
        if (line < 0)
        {
            MessageBeep(MB_OK);
            return;
        }
        pane->SetCaret({ line, 0 }, false, false);
        pane->CenterLine(line, true);
    }

    void CompareWnd::OnClearBookmarks()
    {
        m_doc.ClearBookmarks();
        InvalidateAll();
    }

    void CompareWnd::OnBookmarkComment()
    {
        const int line = Pane(m_active)->Caret().line;
        std::wstring comment = m_doc.Buffer(m_active).GetLine(line).comment;
        CString label;
        label.Format(L"Comment on line %d of %s:", line + 1, m_doc.Buffer(m_active).title.c_str());
        if (!EditBookmarkComment(label, comment, this))
            return;
        m_doc.SetBookmarkComment(m_active, line, comment);
        m_commentLine = -1;  // show the new comment
        m_commentShown = true;
        OnCaretMoved(m_active);
    }

    void CompareWnd::OnPrint()
    {
        PrintComparison(BuildPrintModel(m_doc), false, this);
    }

    void CompareWnd::OnPrintPreview()
    {
        PrintComparison(BuildPrintModel(m_doc), true, this);
    }

    void CompareWnd::OnReport()
    {
        CreateReport(&m_doc, nullptr, this);
    }

    void CompareWnd::OnSaveArchive()
    {
        std::vector<std::wstring> names;
        for (int pane = 0; pane < m_doc.PaneCount(); ++pane)
            names.push_back(m_doc.Buffer(pane).title);
        CWaitCursor wait;
        SaveArchiveAs(SaveComparisonArchive(m_doc), SuggestedName(names), this);
    }

    void CompareWnd::OnUpdateBookmarks(CCmdUI* ui)
    {
        ui->Enable(m_doc.BookmarkCount() > 0);
    }

    void CompareWnd::OnSyncAdd()
    {
        SyncPoint point;
        for (int pane = 0; pane < PaneCount(); ++pane)
            point.line[pane] = Pane(pane)->Caret().line;
        m_doc.AddSyncPoint(point);
        OnTextChanged();
        if (CFrameWnd* frame = GetParentFrame())
            frame->SetMessageText(L"Synchronization link added: the lines at the carets now correspond.");
    }

    void CompareWnd::OnSyncClear()
    {
        m_doc.ClearSyncPoints();
        OnTextChanged();
    }

    void CompareWnd::OnUpdateSyncClear(CCmdUI* ui)
    {
        ui->Enable(!m_doc.SyncPoints().empty());
    }

    void CompareWnd::OnUpdateFormatXml(CCmdUI* ui)
    {
        bool xml = true;
        for (int pane = 0; pane < PaneCount(); ++pane)
            xml = xml && LooksLikeXml(m_doc.Buffer(pane).GetAllText());
        ui->Enable(xml);
    }

    void CompareWnd::OnFormatXml()
    {
        std::vector<std::wstring> texts, titles;
        for (int pane = 0; pane < PaneCount(); ++pane)
        {
            const TextBuffer& buffer = m_doc.Buffer(pane);
            std::wstring formatted, error;
            if (!FormatXml(buffer.GetAllText(), m_view.tabSize, buffer.eol, formatted, error))
            {
                AfxMessageBox((buffer.title + L": " + error).c_str(), MB_ICONEXCLAMATION);
                return;
            }
            texts.push_back(formatted);
            titles.push_back(buffer.title + L" (formatted, read-only)");
        }
        if (MainFrame* frame = dynamic_cast<MainFrame*>(GetParentFrame()))
            frame->OpenTexts(texts, titles);
    }
}
