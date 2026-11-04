// FolderCompareWnd.cpp - background folder comparison, the split file view, and the folder commands.
#include "FolderCompareWnd.h"

#include <shlobj.h>

#include "Archive.h"
#include "Dialogs.h"
#include "DocumentText.h"
#include "Encoding.h"
#include "Packed.h"
#include "ImageDiff.h"
#include "MainFrame.h"
#include "MergeFilesApp.h"
#include "Output.h"
#include "Printing.h"
#include "PaneView.h"
#include "Theme.h"
#include "resource.h"

namespace mf
{
    namespace
    {
        const UINT_PTR kSelectionTimer = 1;
        const UINT kFileId = 600;

        std::wstring LeafName(const std::wstring& path)
        {
            std::wstring trimmed = path;
            while (trimmed.size() > 3 && (trimmed.back() == L'\\' || trimmed.back() == L'/'))
                trimmed.pop_back();
            const size_t slash = trimmed.find_last_of(L"\\/");
            return slash == std::wstring::npos || slash + 1 == trimmed.size() ? trimmed : trimmed.substr(slash + 1);
        }

        const wchar_t* ModeName(FolderCompareMode mode)
        {
            switch (mode)
            {
            case FolderCompareMode::Quick:
                return L"Quick comparison (size and time)";
            case FolderCompareMode::Thorough:
                return L"Thorough comparison (text rules)";
            default:
                return L"Byte-by-byte comparison";
            }
        }
    }

    BEGIN_MESSAGE_MAP(FolderCompareWnd, TabWindow)
        ON_WM_PAINT()
        ON_WM_ERASEBKGND()
        ON_WM_SIZE()
        ON_WM_SETFOCUS()
        ON_WM_DESTROY()
        ON_WM_TIMER()
        ON_WM_LBUTTONDOWN()
        ON_WM_LBUTTONUP()
        ON_WM_MOUSEMOVE()
        ON_WM_SETCURSOR()
        ON_MESSAGE(WM_MF_FOLDERPROGRESS, &FolderCompareWnd::OnProgress)
        ON_MESSAGE(WM_MF_FOLDERDONE, &FolderCompareWnd::OnDone)
        ON_COMMAND(ID_VIEW_START, &FolderCompareWnd::OnStart)
        ON_COMMAND(ID_VIEW_RECOMPARE, &FolderCompareWnd::OnStart)
        ON_COMMAND(ID_VIEW_STOP, &FolderCompareWnd::OnStop)
        ON_UPDATE_COMMAND_UI(ID_VIEW_START, &FolderCompareWnd::OnUpdateStart)
        ON_UPDATE_COMMAND_UI(ID_VIEW_RECOMPARE, &FolderCompareWnd::OnUpdateStart)
        ON_UPDATE_COMMAND_UI(ID_VIEW_STOP, &FolderCompareWnd::OnUpdateStop)
        ON_COMMAND(ID_MERGE_NEXT_CHANGE, &FolderCompareWnd::OnNextChange)
        ON_COMMAND(ID_MERGE_PREV_CHANGE, &FolderCompareWnd::OnPrevChange)
        ON_COMMAND(ID_MERGE_NEXT_CONFLICT, &FolderCompareWnd::OnNextConflict)
        ON_COMMAND(ID_MERGE_PREV_CONFLICT, &FolderCompareWnd::OnPrevConflict)
        ON_UPDATE_COMMAND_UI(ID_MERGE_NEXT_CHANGE, &FolderCompareWnd::OnUpdateNextChange)
        ON_UPDATE_COMMAND_UI(ID_MERGE_PREV_CHANGE, &FolderCompareWnd::OnUpdatePrevChange)
        ON_UPDATE_COMMAND_UI(ID_MERGE_NEXT_CONFLICT, &FolderCompareWnd::OnUpdateNextConflict)
        ON_UPDATE_COMMAND_UI(ID_MERGE_PREV_CONFLICT, &FolderCompareWnd::OnUpdatePrevConflict)
        ON_COMMAND(ID_MERGE_COPY_TO_RIGHT, &FolderCompareWnd::OnCopyToRight)
        ON_COMMAND(ID_MERGE_COPY_TO_LEFT, &FolderCompareWnd::OnCopyToLeft)
        ON_UPDATE_COMMAND_UI(ID_MERGE_COPY_TO_RIGHT, &FolderCompareWnd::OnUpdateCopy)
        ON_UPDATE_COMMAND_UI(ID_MERGE_COPY_TO_LEFT, &FolderCompareWnd::OnUpdateCopy)
        ON_COMMAND(ID_FOLDER_DELETE_LEFT, &FolderCompareWnd::OnDeleteLeft)
        ON_COMMAND(ID_FOLDER_DELETE_RIGHT, &FolderCompareWnd::OnDeleteRight)
        ON_UPDATE_COMMAND_UI(ID_FOLDER_DELETE_LEFT, &FolderCompareWnd::OnUpdateCopy)
        ON_UPDATE_COMMAND_UI(ID_FOLDER_DELETE_RIGHT, &FolderCompareWnd::OnUpdateCopy)
        ON_COMMAND(ID_MERGE_AUTO, &FolderCompareWnd::OnAutoMerge)
        ON_UPDATE_COMMAND_UI(ID_MERGE_AUTO, &FolderCompareWnd::OnUpdateAutoMerge)
        ON_COMMAND(ID_FOLDER_OPEN, &FolderCompareWnd::OnOpenItem)
        ON_COMMAND(ID_FOLDER_OPEN_TAB, &FolderCompareWnd::OnOpenItemTab)
        ON_UPDATE_COMMAND_UI(ID_FOLDER_OPEN, &FolderCompareWnd::OnUpdateHasFile)
        ON_UPDATE_COMMAND_UI(ID_FOLDER_OPEN_TAB, &FolderCompareWnd::OnUpdateHasFile)
        ON_COMMAND(ID_FOLDER_EXPAND_ALL, &FolderCompareWnd::OnExpandAll)
        ON_COMMAND(ID_FOLDER_COLLAPSE_ALL, &FolderCompareWnd::OnCollapseAll)
        ON_UPDATE_COMMAND_UI(ID_FOLDER_EXPAND_ALL, &FolderCompareWnd::OnUpdateIdle)
        ON_UPDATE_COMMAND_UI(ID_FOLDER_COLLAPSE_ALL, &FolderCompareWnd::OnUpdateIdle)
        ON_COMMAND_RANGE(ID_FOLDER_SHOW_IDENTICAL, ID_FOLDER_SHOW_DIFFERENT, &FolderCompareWnd::OnShowFilter)
        ON_UPDATE_COMMAND_UI_RANGE(ID_FOLDER_SHOW_IDENTICAL, ID_FOLDER_SHOW_DIFFERENT, &FolderCompareWnd::OnUpdateShowFilter)
        ON_COMMAND(ID_FOLDER_SELECT, &FolderCompareWnd::OnSelectFiles)
        ON_UPDATE_COMMAND_UI(ID_FOLDER_SELECT, &FolderCompareWnd::OnUpdateIdle)
        ON_COMMAND(ID_EDIT_SELECT_ALL, &FolderCompareWnd::OnSelectAll)
        ON_UPDATE_COMMAND_UI(ID_EDIT_SELECT_ALL, &FolderCompareWnd::OnUpdateIdle)
        ON_COMMAND(ID_FOLDER_SPLIT, &FolderCompareWnd::OnToggleSplit)
        ON_UPDATE_COMMAND_UI(ID_FOLDER_SPLIT, &FolderCompareWnd::OnUpdateSplit)
        ON_COMMAND(ID_VIEW_STATISTICS, &FolderCompareWnd::OnStatistics)
        ON_UPDATE_COMMAND_UI(ID_VIEW_STATISTICS, &FolderCompareWnd::OnUpdateIdle)
        ON_UPDATE_COMMAND_UI(ID_VIEW_LAYOUT_TWO, &FolderCompareWnd::OnUpdateLayout)
        ON_UPDATE_COMMAND_UI(ID_VIEW_LAYOUT_THREE, &FolderCompareWnd::OnUpdateLayout)
        ON_COMMAND(ID_FILE_PRINT, &FolderCompareWnd::OnPrint)
        ON_COMMAND(ID_FILE_PRINT_PREVIEW, &FolderCompareWnd::OnPrintPreview)
        ON_COMMAND(ID_FILE_REPORT, &FolderCompareWnd::OnReport)
        ON_COMMAND(ID_FILE_SAVE_ARCHIVE, &FolderCompareWnd::OnSaveArchive)
        ON_UPDATE_COMMAND_UI(ID_FILE_PRINT, &FolderCompareWnd::OnUpdateIdle)
        ON_UPDATE_COMMAND_UI(ID_FILE_PRINT_PREVIEW, &FolderCompareWnd::OnUpdateIdle)
        ON_UPDATE_COMMAND_UI(ID_FILE_REPORT, &FolderCompareWnd::OnUpdateIdle)
        ON_UPDATE_COMMAND_UI(ID_FILE_SAVE_ARCHIVE, &FolderCompareWnd::OnUpdateIdle)
    END_MESSAGE_MAP()

    FolderCompareWnd::FolderCompareWnd() : m_tree(*this)
    {
    }

    FolderCompareWnd::~FolderCompareWnd()
    {
        StopCompare();
    }

    BOOL FolderCompareWnd::Create(CWnd* parent, UINT id)
    {
        const CString className = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW));
        if (!CWnd::Create(className, L"Folder comparison", WS_CHILD | WS_CLIPCHILDREN, CRect(), parent, id))
            return FALSE;
        m_tree.Create(this);
        m_file = std::make_unique<CompareWnd>();
        m_file->Create(this, kFileId);
        m_file->SetViewSettings(App().viewSettings);
        m_file->Doc().SetOptions(App().compareOptions);
        m_file->onSaved = [this]() {
            // A saved file may now match (or differ from) the other side.
            if (m_comparison && m_fileItem >= 0)
                AfterChange({ m_comparison->Item(m_fileItem).relPath });
        };
        m_file->ShowWindow(SW_SHOW);
        return TRUE;
    }

    void FolderCompareWnd::Open(const std::vector<std::wstring>& roots)
    {
        m_roots = roots;
        // The file view below gets as many (empty) panes as there are folders.
        CString error;
        m_file->OpenFiles(std::vector<std::wstring>(roots.size()), error);
        for (int pane = 0; pane < static_cast<int>(roots.size()); ++pane)
            m_file->Doc().Buffer(pane).title = L"Select a file above to compare it here";
        m_file->OnTextChanged();
        StartCompare();
        Layout();
    }

    void FolderCompareWnd::OpenArchive(std::unique_ptr<FolderComparison> comparison)
    {
        StopCompare();
        m_roots.clear();
        for (int side = 0; side < comparison->SideCount(); ++side)
            m_roots.push_back(comparison->Root(side));
        CString error;
        m_file->OpenFiles(std::vector<std::wstring>(m_roots.size()), error);
        for (int pane = 0; pane < static_cast<int>(m_roots.size()); ++pane)
            m_file->Doc().Buffer(pane).title = L"Select a file above to compare it here";
        m_file->OnTextChanged();
        m_options = comparison->Options();
        m_comparison = std::move(comparison);
        m_snapshot = true;
        m_fileItem = -1;
        m_fileKey.clear();
        m_tree.Rebuild();
        Layout();
        if (CFrameWnd* frame = GetParentFrame())
            frame->SetMessageText(L"Archived folder comparison: press Start (F5) to compare the folders as they are now.");
        NotifyFrame();
        Invalidate(FALSE);
    }

    // ---- background comparison ----

    void FolderCompareWnd::StartCompare()
    {
        StopCompare();
        m_snapshot = false;
        m_options = App().folderOptions;
        m_options.text = App().compareOptions;
        m_running = std::make_unique<FolderComparison>(m_roots, m_options);
        m_cancel = false;
        m_stopped = false;
        {
            std::lock_guard<std::mutex> lock(m_progressLock);
            m_progress = FolderProgress();
        }
        FolderComparison* comparison = m_running.get();
        const HWND window = m_hWnd;
        const unsigned generation = ++m_generation;
        m_thread = std::thread([this, comparison, window, generation]() {
            const bool complete = comparison->Run(&m_cancel, [this, window](const FolderProgress& progress) {
                {
                    std::lock_guard<std::mutex> lock(m_progressLock);
                    m_progress = progress;
                }
                // At most a few updates a second reach the window.
                const unsigned long long now = GetTickCount64();
                if (now - m_lastPost > 150)
                {
                    m_lastPost = now;
                    ::PostMessage(window, WM_MF_FOLDERPROGRESS, 0, 0);
                }
            });
            ::PostMessage(window, WM_MF_FOLDERDONE, complete ? 1 : 0, static_cast<LPARAM>(generation));
        });
        NotifyFrame();
        Invalidate(FALSE);
    }

    void FolderCompareWnd::StopCompare()
    {
        if (m_thread.joinable())
        {
            m_cancel = true;
            m_thread.join();
        }
    }

    bool FolderCompareWnd::Busy() const
    {
        return m_running != nullptr;
    }

    LRESULT FolderCompareWnd::OnProgress(WPARAM, LPARAM)
    {
        NotifyFrame();
        return 0;
    }

    LRESULT FolderCompareWnd::OnDone(WPARAM complete, LPARAM generation)
    {
        // A run that was replaced by a newer one (its thread is already joined) is ignored.
        if (static_cast<unsigned>(generation) != m_generation)
            return 0;
        if (m_thread.joinable())
            m_thread.join();
        if (!m_running)
            return 0;
        m_stopped = complete == 0;
        m_comparison = std::move(m_running);
        m_tree.Rebuild();
        if (m_fileItem >= 0)
        {
            m_fileItem = m_comparison->FindItem(m_fileKey);
            if (m_fileItem < 0)
                m_fileKey.clear();
        }
        if (CFrameWnd* frame = GetParentFrame())
            frame->SetMessageText(m_stopped ? L"Folder comparison stopped: the results are incomplete." : L"Folder comparison finished.");
        NotifyFrame();
        Invalidate(FALSE);
        return 0;
    }

    void FolderCompareWnd::OnDestroy()
    {
        StopCompare();
        KillTimer(kSelectionTimer);
        TabWindow::OnDestroy();
    }

    // ---- TabWindow ----

    TabItem FolderCompareWnd::GetTabItem() const
    {
        TabItem item;
        item.kind = TabKind::Folder;
        for (const std::wstring& root : m_roots)
        {
            if (!item.title.IsEmpty())
                item.title += L", ";
            item.title += LeafName(root).c_str();
            if (!item.tooltip.IsEmpty())
                item.tooltip += L"\n";
            item.tooltip += root.c_str();
        }
        if (m_snapshot)
        {
            item.title += L" (archived)";
            item.tooltip += L"\nResults from a comparison archive";
        }
        item.modified = IsModified();
        return item;
    }

    bool FolderCompareWnd::IsModified() const
    {
        return m_file && m_fileItem >= 0 && m_file->IsModified();
    }

    bool FolderCompareWnd::QueryClose()
    {
        if (m_file && m_fileItem >= 0 && !m_file->QueryClose())
            return false;
        StopCompare();
        return true;
    }

    void FolderCompareWnd::ApplySettings()
    {
        if (m_file)
            m_file->ApplySettings();
        const FolderOptions& wanted = App().folderOptions;
        const bool changed = wanted.mode != m_options.mode || wanted.include != m_options.include || wanted.exclude != m_options.exclude ||
            wanted.thoroughLimit != m_options.thoroughLimit || wanted.mode == FolderCompareMode::Thorough;
        if (changed && !m_roots.empty())
            StartCompare();
    }

    void FolderCompareWnd::UpdateMetrics()
    {
        m_tree.UpdateMetrics();
        if (m_file)
            m_file->UpdateMetrics();
        Layout();
    }

    CompareWnd* FolderCompareWnd::TextComparison()
    {
        return m_split && m_fileItem >= 0 ? m_file.get() : nullptr;
    }

    CString FolderCompareWnd::StatusText(int part) const
    {
        if (part == 0)
        {
            if (Busy())
            {
                std::lock_guard<std::mutex> lock(m_progressLock);
                CString text;
                text.Format(L"Comparing\x2026 %d items found, %d files compared", m_progress.scanned, m_progress.compared);
                return text;
            }
            return ModeName(m_options.mode);
        }
        if (!m_comparison)
            return L"";
        if (part == 1)
        {
            const FolderComparison::Counts counts = m_comparison->CountFiles();
            CString text;
            if (m_comparison->IsThreeWay())
                text.Format(L"%d unchanged \x2022 %d left \x2022 %d right \x2022 %d both \x2022 %d conflicts", counts.identical, counts.leftChanged, counts.rightChanged,
                    counts.bothSame, counts.conflicts);
            else
                text.Format(L"%d identical \x2022 %d different \x2022 %d left only \x2022 %d right only", counts.identical, counts.different, counts.leftOnly, counts.rightOnly);
            if (counts.errors)
            {
                CString errors;
                errors.Format(L" \x2022 %d errors", counts.errors);
                text += errors;
            }
            if (m_stopped)
                text += L" (stopped)";
            return text;
        }
        const int item = m_tree.FocusedItem();
        if (item < 0)
            return L"";
        const FolderItem& entry = m_comparison->Item(item);
        CString text = entry.relPath.c_str();
        if (!entry.note.empty())
            text += (L" \x2014 " + entry.note).c_str();
        return text;
    }

    BOOL FolderCompareWnd::OnCmdMsg(UINT id, int code, void* extra, AFX_CMDHANDLERINFO* handlerInfo)
    {
        // While the file comparison below has the focus, it handles its commands first.
        const bool fileShown = m_file && m_split && m_fileItem >= 0;
        CWnd* focus = GetFocus();
        const bool fileFocused = fileShown && focus && (focus == m_file.get() || m_file->IsChild(focus));
        if (fileFocused && m_file->OnCmdMsg(id, code, extra, handlerInfo))
            return TRUE;
        if (TabWindow::OnCmdMsg(id, code, extra, handlerInfo))
            return TRUE;
        return fileShown && !fileFocused && m_file->OnCmdMsg(id, code, extra, handlerInfo);
    }

    void FolderCompareWnd::NotifyFrame()
    {
        if (CFrameWnd* frame = GetParentFrame())
            frame->PostMessage(WM_MF_STATECHANGED);
    }

    std::wstring FolderCompareWnd::ExtractItem(int item, int side, std::wstring& error)
    {
        std::string bytes;
        if (!m_comparison->ReadItem(item, side, bytes, error))
            return L"";
        const std::wstring path = TempFolder() + L"\\" + std::to_wstring(std::hash<std::wstring>()(m_comparison->Root(side))) + L"\\" +
                                  m_comparison->Item(item).relPath;
        const std::wstring parent = path.substr(0, path.find_last_of(L'\\'));
        SHCreateDirectoryExW(nullptr, parent.c_str(), nullptr);
        if (!WriteFileBytes(path, bytes, error))
            return L"";
        return path;
    }

    std::wstring FolderCompareWnd::SideName(int side) const
    {
        if (m_roots.size() == 3)
            return side == 0 ? L"left" : (side == 1 ? L"ancestor" : L"right");
        return side == 0 ? L"left" : L"right";
    }

    // ---- layout and painting ----

    CRect FolderCompareWnd::SplitterRect() const
    {
        CRect client;
        GetClientRect(&client);
        const int top = GetFonts().Scale(30);
        const int height = client.Height() - top;
        const int y = top + static_cast<int>(height * m_splitRatio);
        return CRect(0, y, client.right, y + GetFonts().Scale(6));
    }

    void FolderCompareWnd::Layout()
    {
        if (!m_hWnd || !m_tree.m_hWnd)
            return;
        CRect client;
        GetClientRect(&client);
        const int top = GetFonts().Scale(30);
        if (m_split)
        {
            const CRect splitter = SplitterRect();
            m_tree.MoveWindow(0, top, client.Width(), (std::max)(0, static_cast<int>(splitter.top) - top));
            m_file->MoveWindow(0, splitter.bottom, client.Width(), (std::max)(0, static_cast<int>(client.bottom - splitter.bottom)));
            m_file->ShowWindow(SW_SHOW);
        }
        else
        {
            m_tree.MoveWindow(0, top, client.Width(), (std::max)(0, static_cast<int>(client.Height()) - top));
            m_file->ShowWindow(SW_HIDE);
        }
        Invalidate(FALSE);
    }

    void FolderCompareWnd::OnSize(UINT type, int cx, int cy)
    {
        TabWindow::OnSize(type, cx, cy);
        Layout();
    }

    BOOL FolderCompareWnd::OnEraseBkgnd(CDC*)
    {
        return TRUE;
    }

    void FolderCompareWnd::OnPaint()
    {
        CPaintDC dc(this);
        CRect client;
        GetClientRect(&client);
        const Theme& theme = GetTheme();
        Fonts& fonts = GetFonts();
        const int pathHeight = fonts.Scale(30);
        dc.FillSolidRect(0, 0, client.Width(), pathHeight, theme.chromeBack);
        if (m_split)
        {
            const CRect splitter = SplitterRect();
            dc.FillSolidRect(&splitter, theme.chromeBack);
            dc.FillSolidRect(splitter.left, splitter.CenterPoint().y, splitter.Width(), 1, theme.chromeBorder);
        }
        dc.SetBkMode(TRANSPARENT);
        const int count = static_cast<int>(m_roots.size());
        if (count == 0)
            return;
        const int gap = fonts.Scale(8);
        const int width = (client.Width() - gap * (count + 1)) / count;
        static const wchar_t* twoWay[] = { L"LEFT FOLDER", L"RIGHT FOLDER" };
        static const wchar_t* threeWay[] = { L"LEFT FOLDER", L"ANCESTOR \x00B7 MERGE", L"RIGHT FOLDER" };
        for (int side = 0; side < count; ++side)
        {
            CRect bar(gap + side * (width + gap), fonts.Scale(3), gap + side * (width + gap) + width, pathHeight - fonts.Scale(3));
            CBrush back(theme.pathBarBack);
            CPen border(PS_SOLID, 1, theme.chromeBorder);
            CBrush* ob = dc.SelectObject(&back);
            CPen* op = dc.SelectObject(&border);
            dc.RoundRect(&bar, CPoint(fonts.Scale(6), fonts.Scale(6)));
            dc.SelectObject(ob);
            dc.SelectObject(op);
            CRect text = bar;
            text.DeflateRect(fonts.Scale(8), 0);
            DrawGlyph(dc, CRect(text.left, bar.top, text.left + fonts.Scale(16), bar.bottom), 0xE8B7, RGB(200, 150, 40), fonts.iconsSmall);
            text.left += fonts.Scale(22);
            CFont* oldFont = dc.SelectObject(&fonts.uiSmall);
            const wchar_t* badge = count == 3 ? threeWay[side] : twoWay[side];
            const int badgeWidth = dc.GetTextExtent(badge).cx;
            CRect badgeRect(text.right - badgeWidth, bar.top, text.right, bar.bottom);
            dc.SetTextColor(theme.chromeLabel);
            dc.DrawText(badge, -1, &badgeRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            text.right = badgeRect.left - fonts.Scale(8);
            dc.SelectObject(&fonts.ui);
            dc.SetTextColor(theme.text);
            dc.DrawText(m_roots[static_cast<size_t>(side)].c_str(), -1, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
            dc.SelectObject(oldFont);
        }
    }

    void FolderCompareWnd::OnSetFocus(CWnd*)
    {
        m_tree.SetFocus();
    }

    void FolderCompareWnd::OnLButtonDown(UINT, CPoint point)
    {
        if (m_split && SplitterRect().PtInRect(point))
        {
            m_draggingSplitter = true;
            SetCapture();
        }
    }

    void FolderCompareWnd::OnMouseMove(UINT, CPoint point)
    {
        if (!m_draggingSplitter)
            return;
        CRect client;
        GetClientRect(&client);
        const int top = GetFonts().Scale(30);
        const int height = (std::max)(1, static_cast<int>(client.Height()) - top);
        m_splitRatio = (std::max)(0.15, (std::min)(0.85, static_cast<double>(point.y - top) / height));
        Layout();
    }

    void FolderCompareWnd::OnLButtonUp(UINT, CPoint)
    {
        if (m_draggingSplitter)
        {
            m_draggingSplitter = false;
            ReleaseCapture();
        }
    }

    BOOL FolderCompareWnd::OnSetCursor(CWnd* wnd, UINT hitTest, UINT message)
    {
        CPoint point;
        GetCursorPos(&point);
        ScreenToClient(&point);
        if (hitTest == HTCLIENT && m_split && SplitterRect().PtInRect(point))
        {
            SetCursor(::LoadCursor(nullptr, IDC_SIZENS));
            return TRUE;
        }
        return TabWindow::OnSetCursor(wnd, hitTest, message);
    }

    // ---- rows ----

    bool FolderCompareWnd::IsItemVisible(const FolderItem& item) const
    {
        switch (item.status)
        {
        case ItemStatus::Identical:
            return m_showIdentical;
        case ItemStatus::LeftOnly:
        case ItemStatus::LeftChanged:
            return m_showLeft;
        case ItemStatus::RightOnly:
        case ItemStatus::RightChanged:
            return m_showRight;
        default:
            return m_showDifferent;
        }
    }

    void FolderCompareWnd::OnSelectionChanged()
    {
        NotifyFrame();
        if (m_split)
            SetTimer(kSelectionTimer, 200, nullptr);
    }

    void FolderCompareWnd::OnTimer(UINT_PTR id)
    {
        if (id != kSelectionTimer)
        {
            TabWindow::OnTimer(id);
            return;
        }
        KillTimer(kSelectionTimer);
        // Show the focused file below, unless the file shown there has unsaved changes.
        const int item = m_tree.FocusedItem();
        if (!m_comparison || item < 0 || item == m_fileItem || m_comparison->Item(item).isDir || m_tree.SelectedItems().size() > 1)
            return;
        if (IsModified())
        {
            if (CFrameWnd* frame = GetParentFrame())
                frame->SetMessageText(L"The file comparison below has unsaved changes: save them, or press Enter to switch files.");
            return;
        }
        m_autoOpening = true;
        OpenItem(item, false);
        m_autoOpening = false;
    }

    void FolderCompareWnd::OpenItem(int item, bool newTab)
    {
        if (!m_comparison || item < 0)
            return;
        const FolderItem& entry = m_comparison->Item(item);
        if (entry.isDir)
            return;
        const int sides = m_comparison->SideCount();
        auto exists = [&](int side) { return entry.side[side].exists && !entry.side[side].isDir; };
        auto archiveName = [&](int side) { return LeafName(m_comparison->Root(side)); };

        // Word, Excel, PowerPoint, OpenDocument and RTF files: their text, read-only.
        if (IsDocumentFileName(entry.name))
        {
            if (m_fileItem >= 0 && m_file->IsModified() && !m_file->QueryClose())
                return;
            if (!m_split)
            {
                m_split = true;
                Layout();
            }
            CString error;
            m_file->OpenFiles(std::vector<std::wstring>(static_cast<size_t>(sides)), error);
            for (int side = 0; side < sides; ++side)
            {
                std::wstring text, message;
                std::string bytes;
                if (exists(side) && (!m_comparison->ReadItem(item, side, bytes, message) || !ExtractDocumentText(bytes, entry.name, text, message)))
                    text = L"(" + message + L")";
                m_file->Doc().SetText(side, text, entry.name + (exists(side) ? L" (text)" : L" (missing)"));
                m_file->Doc().Buffer(side).readOnly = true;
            }
            m_fileItem = item;
            m_fileKey = entry.relPath;
            m_file->OnTextChanged();
            m_file->GoToFirstDifference();
            if (CFrameWnd* frame = GetParentFrame())
                frame->SetMessageText(L"Documents are compared by their text (read-only).");
            NotifyFrame();
            return;
        }

        // Files inside archives are extracted to a temporary folder first.
        std::vector<std::wstring> paths;
        for (int side = 0; side < sides; ++side)
        {
            std::wstring path;
            if (exists(side))
            {
                std::wstring error;
                path = m_comparison->IsArchiveSide(side) ? ExtractItem(item, side, error) : m_comparison->FullPath(item, side);
                if (path.empty())
                {
                    AfxMessageBox(error.c_str(), MB_ICONERROR);
                    return;
                }
            }
            paths.push_back(path);
        }
        // Images, binary files and archives inside the folders get a tab of their own (when asked for).
        bool special = false, nested = false;
        for (const std::wstring& path : paths)
        {
            std::string head;
            std::wstring error;
            nested = nested || (!path.empty() && IsPackedArchiveName(path));
            special = special || (!path.empty() && (IsImageFileName(path) || IsPackedArchiveName(path) || (ReadFileBytes(path, head, error) && LooksBinary(head.substr(0, 65536)))));
        }
        if (special && !newTab)
        {
            if (m_autoOpening)
            {
                if (CFrameWnd* frame = GetParentFrame())
                    frame->SetMessageText(nested ? L"An archive: press Enter to compare its contents in a tab of its own."
                                                 : L"An image or binary file: press Enter to compare it in a tab of its own.");
                return;
            }
            newTab = true;
        }
        if (newTab)
        {
            if (MainFrame* frame = dynamic_cast<MainFrame*>(GetParentFrame()))
                frame->OpenComparison(paths);
            return;
        }
        if (m_fileItem >= 0 && m_file->IsModified() && !m_file->QueryClose())
            return;
        if (!m_split)
        {
            m_split = true;
            Layout();
        }
        CString error;
        if (!m_file->OpenFiles(paths, error))
        {
            m_fileItem = -1;
            m_fileKey.clear();
            m_file->OpenFiles(std::vector<std::wstring>(paths.size()), error);
            if (CFrameWnd* frame = GetParentFrame())
                frame->SetMessageText(L"Cannot show this file comparison (binary or unreadable file).");
            return;
        }
        for (int side = 0; side < sides; ++side)
        {
            TextBuffer& buffer = m_file->Doc().Buffer(side);
            if (m_comparison->IsArchiveSide(side))
            {
                // Archives are read-only: the extracted copy is not saved back.
                buffer.path.clear();
                buffer.title = entry.name + (exists(side) ? L" (in " + archiveName(side) + L")" : L" (missing)");
                buffer.readOnly = true;
            }
            else if (paths[static_cast<size_t>(side)].empty())
            {
                // A side where the file is missing saves to where it would be.
                buffer.path = m_comparison->FullPath(item, side);
                buffer.title = entry.name + L" (missing)";
            }
        }
        m_fileItem = item;
        m_fileKey = entry.relPath;
        m_file->OnTextChanged();
        m_file->GoToFirstDifference();
        NotifyFrame();
    }

    void FolderCompareWnd::ShowItemMenu(CPoint screenPoint)
    {
        CMenu menu;
        if (!menu.LoadMenuW(IDR_POPUPS))
            return;
        if (CMenu* popup = menu.GetSubMenu(POPUP_FOLDER))
            popup->TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, screenPoint.x, screenPoint.y, GetParentFrame());
    }

    std::vector<int> FolderCompareWnd::TopSelection() const
    {
        std::vector<int> selected = m_tree.SelectedItems();
        if (selected.empty() && m_tree.FocusedItem() >= 0)
            selected.push_back(m_tree.FocusedItem());
        std::vector<int> top;
        for (int item : selected)
        {
            bool inside = false;
            for (int parent = m_comparison->Item(item).parent; parent > 0 && !inside; parent = m_comparison->Item(parent).parent)
                inside = std::find(selected.begin(), selected.end(), parent) != selected.end();
            if (!inside)
                top.push_back(item);
        }
        return top;
    }

    void FolderCompareWnd::AfterChange(const std::vector<std::wstring>& changedPaths)
    {
        for (const std::wstring& path : changedPaths)
        {
            const int item = m_comparison->FindItem(path);
            if (item > 0)
                m_comparison->RefreshItem(item);
        }
        m_tree.Rebuild();
        if (m_fileItem >= 0)
            m_fileItem = m_comparison->FindItem(m_fileKey);
        NotifyFrame();
    }

    // ---- commands ----

    void FolderCompareWnd::OnStart()
    {
        if (m_fileItem >= 0 && m_file->IsModified() && !m_file->QueryClose())
            return;
        StartCompare();
    }

    void FolderCompareWnd::OnStop()
    {
        m_cancel = true;
    }

    void FolderCompareWnd::OnUpdateStart(CCmdUI* ui)
    {
        ui->Enable(!Busy());
    }

    void FolderCompareWnd::OnUpdateStop(CCmdUI* ui)
    {
        ui->Enable(Busy());
    }

    void FolderCompareWnd::OnUpdateIdle(CCmdUI* ui)
    {
        ui->Enable(m_comparison != nullptr && !Busy());
    }

    void FolderCompareWnd::OnPrint()
    {
        if (m_comparison && !Busy())
            PrintComparison(BuildFolderPrintModel(*m_comparison), false, this);
    }

    void FolderCompareWnd::OnPrintPreview()
    {
        if (m_comparison && !Busy())
            PrintComparison(BuildFolderPrintModel(*m_comparison), true, this);
    }

    void FolderCompareWnd::OnReport()
    {
        if (m_comparison && !Busy())
            CreateReport(nullptr, m_comparison.get(), this);
    }

    void FolderCompareWnd::OnSaveArchive()
    {
        if (!m_comparison || Busy())
            return;
        CWaitCursor wait;
        SaveArchiveAs(SaveFolderArchive(*m_comparison), SuggestedName(m_roots), this);
    }

    void FolderCompareWnd::OnUpdateLayout(CCmdUI* ui)
    {
        ui->Enable(FALSE);
        ui->SetRadio((ui->m_nID == ID_VIEW_LAYOUT_THREE) == (m_roots.size() == 3));
    }

    void FolderCompareWnd::OnNextChange()
    {
        m_tree.GoToDifference(true, false);
    }

    void FolderCompareWnd::OnPrevChange()
    {
        m_tree.GoToDifference(false, false);
    }

    void FolderCompareWnd::OnNextConflict()
    {
        m_tree.GoToDifference(true, true);
    }

    void FolderCompareWnd::OnPrevConflict()
    {
        m_tree.GoToDifference(false, true);
    }

    void FolderCompareWnd::OnUpdateNextChange(CCmdUI* ui)
    {
        ui->Enable(!Busy() && m_tree.CanGoToDifference(true, false));
    }

    void FolderCompareWnd::OnUpdatePrevChange(CCmdUI* ui)
    {
        ui->Enable(!Busy() && m_tree.CanGoToDifference(false, false));
    }

    void FolderCompareWnd::OnUpdateNextConflict(CCmdUI* ui)
    {
        ui->Enable(!Busy() && m_roots.size() == 3 && m_tree.CanGoToDifference(true, true));
    }

    void FolderCompareWnd::OnUpdatePrevConflict(CCmdUI* ui)
    {
        ui->Enable(!Busy() && m_roots.size() == 3 && m_tree.CanGoToDifference(false, true));
    }

    void FolderCompareWnd::OnUpdateCopy(CCmdUI* ui)
    {
        ui->Enable(m_comparison && !Busy() && !m_snapshot && (m_tree.FocusedItem() > 0 || !m_tree.SelectedItems().empty()));
    }

    void FolderCompareWnd::CopySelection(bool toRight)
    {
        if (!m_comparison || Busy() || m_snapshot)
            return;
        const bool threeWay = m_comparison->IsThreeWay();
        const int from = toRight ? 0 : (threeWay ? 2 : 1);
        const int to = threeWay ? 1 : (toRight ? 1 : 0);
        const std::vector<int> items = TopSelection();
        if (items.empty())
            return;
        int deletes = 0;
        for (int item : items)
            deletes += m_comparison->Item(item).side[from].exists ? 0 : 1;
        CString question;
        question.Format(L"Copy %d item%s from the %s folder to the %s folder?\n\nExisting files there are overwritten.%s", static_cast<int>(items.size()),
            items.size() == 1 ? L"" : L"s", SideName(from).c_str(), SideName(to).c_str(),
            deletes ? L"\nItems missing on the source side are deleted (moved to the Recycle Bin)." : L"");
        if (AfxMessageBox(question, MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
            return;
        if (m_fileItem >= 0 && m_file->IsModified() && !m_file->QueryClose())
            return;
        CWaitCursor wait;
        std::vector<std::wstring> paths, errors;
        for (int item : items)
            paths.push_back(m_comparison->Item(item).relPath);
        for (const std::wstring& path : paths)
        {
            std::wstring error;
            const int item = m_comparison->FindItem(path);
            if (item > 0 && !m_comparison->CopyItem(item, from, to, error))
                errors.push_back(error);
        }
        AfterChange({});
        if (m_fileItem >= 0)
        {
            const int item = m_fileItem;
            m_fileItem = -1;
            OpenItem(item, false);
        }
        if (!errors.empty())
            AfxMessageBox((L"Some items could not be copied:\n\n" + errors.front()).c_str(), MB_ICONEXCLAMATION);
    }

    void FolderCompareWnd::OnCopyToRight()
    {
        CopySelection(true);
    }

    void FolderCompareWnd::OnCopyToLeft()
    {
        CopySelection(false);
    }

    void FolderCompareWnd::DeleteSelection(int side)
    {
        if (!m_comparison || Busy() || m_snapshot)
            return;
        std::vector<int> items;
        for (int item : TopSelection())
        {
            if (m_comparison->Item(item).side[side].exists)
                items.push_back(item);
        }
        if (items.empty())
        {
            MessageBeep(MB_OK);
            return;
        }
        CString question;
        question.Format(L"Move %d item%s in the %s folder to the Recycle Bin?", static_cast<int>(items.size()), items.size() == 1 ? L"" : L"s", SideName(side).c_str());
        if (AfxMessageBox(question, MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
            return;
        CWaitCursor wait;
        std::vector<std::wstring> paths;
        for (int item : items)
            paths.push_back(m_comparison->Item(item).relPath);
        for (const std::wstring& path : paths)
        {
            std::wstring error;
            const int item = m_comparison->FindItem(path);
            if (item > 0 && !m_comparison->DeleteItem(item, side, error))
            {
                AfxMessageBox(error.c_str(), MB_ICONEXCLAMATION);
                break;
            }
        }
        AfterChange({});
    }

    void FolderCompareWnd::OnDeleteLeft()
    {
        DeleteSelection(0);
    }

    void FolderCompareWnd::OnDeleteRight()
    {
        DeleteSelection(m_comparison && m_comparison->IsThreeWay() ? 2 : 1);
    }

    void FolderCompareWnd::OnUpdateAutoMerge(CCmdUI* ui)
    {
        ui->Enable(m_comparison && !Busy() && !m_snapshot && m_comparison->IsThreeWay() && m_comparison->Item(0).status != ItemStatus::Identical);
    }

    void FolderCompareWnd::OnAutoMerge()
    {
        if (!m_comparison || !m_comparison->IsThreeWay() || Busy() || m_snapshot)
            return;
        if (m_fileItem >= 0 && m_file->IsModified() && !m_file->QueryClose())
            return;
        CWaitCursor wait;
        const FolderComparison::MergeReport preview = m_comparison->AutoMerge(true);
        if (preview.applied == 0 && preview.textMerged == 0)
        {
            AfxMessageBox(L"There is nothing to merge automatically.", MB_ICONINFORMATION);
            return;
        }
        CString question;
        question.Format(L"Merge into the ancestor folder?\n\n%s\n\n%d item(s) will be taken from one side and %d file(s) merged line by line.\n"
                        L"%d conflict(s) will be left for you. Replaced and removed files cannot be restored by Undo (removed ones go to the Recycle Bin).",
            m_roots[1].c_str(), preview.applied, preview.textMerged, static_cast<int>(preview.conflicts.size()));
        if (AfxMessageBox(question, MB_OKCANCEL | MB_ICONQUESTION) != IDOK)
            return;
        const FolderComparison::MergeReport report = m_comparison->AutoMerge(false);
        AfterChange({});
        CString message;
        message.Format(L"Merged: %d item(s) taken from one side, %d file(s) merged line by line. %d conflict(s) left.", report.applied, report.textMerged,
            static_cast<int>(report.conflicts.size()));
        if (!report.errors.empty())
            AfxMessageBox((message + L"\n\nErrors:\n" + report.errors.front().c_str()), MB_ICONEXCLAMATION);
        if (CFrameWnd* frame = GetParentFrame())
            frame->SetMessageText(message);
        m_tree.GoToDifference(true, true);
    }

    void FolderCompareWnd::OnOpenItem()
    {
        OpenItem(m_tree.FocusedItem(), false);
    }

    void FolderCompareWnd::OnOpenItemTab()
    {
        OpenItem(m_tree.FocusedItem(), true);
    }

    void FolderCompareWnd::OnUpdateHasFile(CCmdUI* ui)
    {
        const int item = m_tree.FocusedItem();
        ui->Enable(m_comparison && item > 0 && !m_comparison->Item(item).isDir);
    }

    void FolderCompareWnd::OnExpandAll()
    {
        m_tree.ExpandAll(true);
    }

    void FolderCompareWnd::OnCollapseAll()
    {
        m_tree.ExpandAll(false);
    }

    void FolderCompareWnd::OnShowFilter(UINT id)
    {
        bool* flag = id == ID_FOLDER_SHOW_IDENTICAL ? &m_showIdentical : id == ID_FOLDER_SHOW_LEFT ? &m_showLeft : id == ID_FOLDER_SHOW_RIGHT ? &m_showRight : &m_showDifferent;
        *flag = !*flag;
        m_tree.Rebuild();
    }

    void FolderCompareWnd::OnUpdateShowFilter(CCmdUI* ui)
    {
        const bool* flag = ui->m_nID == ID_FOLDER_SHOW_IDENTICAL ? &m_showIdentical
            : ui->m_nID == ID_FOLDER_SHOW_LEFT                   ? &m_showLeft
            : ui->m_nID == ID_FOLDER_SHOW_RIGHT                  ? &m_showRight
                                                                 : &m_showDifferent;
        ui->Enable(TRUE);
        ui->SetCheck(*flag);
    }

    void FolderCompareWnd::OnSelectFiles()
    {
        SelectFilesDlg dialog(this);
        if (dialog.DoModal() != IDOK)
            return;
        const std::wstring pattern = dialog.pattern.empty() ? L"*" : dialog.pattern;
        const int count = m_tree.SelectWhere([&](const FolderItem& item) {
            if (!MatchesPatternList(pattern, item.name, false))
                return false;
            switch (item.status)
            {
            case ItemStatus::Identical:
                return dialog.identical;
            case ItemStatus::LeftOnly:
            case ItemStatus::LeftChanged:
                return dialog.left;
            case ItemStatus::RightOnly:
            case ItemStatus::RightChanged:
                return dialog.right;
            default:
                return dialog.different;
            }
        });
        CString message;
        message.Format(L"%d file%s selected.", count, count == 1 ? L"" : L"s");
        if (CFrameWnd* frame = GetParentFrame())
            frame->SetMessageText(message);
        m_tree.SetFocus();
    }

    void FolderCompareWnd::OnSelectAll()
    {
        m_tree.SelectWhere([](const FolderItem&) { return true; });
    }

    void FolderCompareWnd::OnToggleSplit()
    {
        if (m_split && IsModified() && !m_file->QueryClose())
            return;
        m_split = !m_split;
        if (!m_split)
        {
            m_fileItem = -1;
            m_fileKey.clear();
        }
        Layout();
        m_tree.SetFocus();
        OnSelectionChanged();
    }

    void FolderCompareWnd::OnUpdateSplit(CCmdUI* ui)
    {
        ui->Enable(TRUE);
        ui->SetCheck(m_split);
    }

    void FolderCompareWnd::OnStatistics()
    {
        if (!m_comparison)
            return;
        const FolderComparison::Counts counts = m_comparison->CountFiles();
        CString text;
        for (int side = 0; side < m_comparison->SideCount(); ++side)
        {
            CString line;
            line.Format(L"%s: %s\r\n", SideName(side).c_str(), m_roots[static_cast<size_t>(side)].c_str());
            text += line;
        }
        CString body;
        if (m_comparison->IsThreeWay())
            body.Format(L"\r\nFiles:\r\n    %d unchanged\r\n    %d changed on the left\r\n    %d changed on the right\r\n    %d changed the same way on both\r\n    %d conflicts\r\n    %d errors",
                counts.identical, counts.leftChanged, counts.rightChanged, counts.bothSame, counts.conflicts, counts.errors);
        else
            body.Format(L"\r\nFiles:\r\n    %d identical\r\n    %d different\r\n    %d only on the left\r\n    %d only on the right\r\n    %d errors", counts.identical,
                counts.different, counts.leftOnly, counts.rightOnly, counts.errors);
        text += body;
        text += L"\r\n\r\nMode: ";
        text += ModeName(m_options.mode);
        if (!m_options.exclude.empty())
            text += (L"\r\nExcluded: " + m_options.exclude).c_str();
        if (!m_options.include.empty())
            text += (L"\r\nIncluded files: " + m_options.include).c_str();
        TextDlg dialog(IDD_STATISTICS, text, this);
        dialog.DoModal();
    }
}
