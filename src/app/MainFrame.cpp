// MainFrame.cpp - tabs of comparisons, global options, find/replace, drag and drop, status bar.
#include "MainFrame.h"

#include "BinaryCompareWnd.h"
#include "FolderCompareWnd.h"
#include "ImageCompareWnd.h"
#include "ImageDiff.h"
#include "PatchWnd.h"
#include "Archive.h"
#include "DocumentText.h"
#include "Packed.h"
#include "MergeFilesApp.h"
#include "Output.h"
#include "OptionsDlg.h"
#include "PaneView.h"
#include "Theme.h"
#include "resource.h"

namespace mf
{
    namespace
    {
        const UINT kInactiveCompareId = 0xE000;  // child IDs of hidden comparisons (any but AFX_IDW_PANE_FIRST)
        const UINT kInactiveWelcomeId = 0xE0FF;

        UINT indicators[] = { ID_SEPARATOR, ID_INDICATOR_ENCODING, ID_INDICATOR_CHANGES, ID_INDICATOR_POSITION };
    }

    // ---- welcome panel ----

    BEGIN_MESSAGE_MAP(WelcomeWnd, CWnd)
        ON_WM_PAINT()
        ON_WM_LBUTTONUP()
    END_MESSAGE_MAP()

    void WelcomeWnd::OnPaint()
    {
        CPaintDC dc(this);
        CRect client;
        GetClientRect(&client);
        const Theme& theme = GetTheme();
        Fonts& fonts = GetFonts();
        dc.FillSolidRect(&client, theme.gutterBack);
        dc.SetBkMode(TRANSPARENT);
        CRect icon(client.left, client.CenterPoint().y - fonts.Scale(90), client.right, client.CenterPoint().y - fonts.Scale(30));
        LOGFONTW lf = {};
        fonts.icons.GetLogFont(&lf);
        lf.lfHeight = -fonts.Scale(44);
        CFont big;
        big.CreateFontIndirectW(&lf);
        DrawGlyph(dc, icon, 0xE8A5, theme.chromeLabel, big);

        CFont* old = dc.SelectObject(&fonts.uiBold);
        dc.SetTextColor(theme.text);
        CRect title(client.left, icon.bottom + fonts.Scale(10), client.right, icon.bottom + fonts.Scale(34));
        dc.DrawText(L"Compare and merge files", &title, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        dc.SelectObject(&fonts.ui);
        dc.SetTextColor(theme.chromeLabel);
        CRect hint(client.left, title.bottom, client.right, title.bottom + fonts.Scale(60));
        dc.DrawText(L"Click here (or press Ctrl+N) to choose files, or drop two or three files on this window.\n"
                    L"Three files are compared as left, common ancestor and right, and merged into the ancestor.",
            &hint, DT_CENTER | DT_WORDBREAK);
        dc.SelectObject(old);
    }

    void WelcomeWnd::OnLButtonUp(UINT, CPoint)
    {
        GetParentFrame()->PostMessage(WM_COMMAND, ID_FILE_NEW_TWO_WAY);
    }

    // ---- frame ----

    BEGIN_MESSAGE_MAP(MainFrame, CFrameWnd)
        ON_WM_CREATE()
        ON_WM_CLOSE()
        ON_WM_SETFOCUS()
        ON_WM_DROPFILES()
        ON_MESSAGE(WM_MF_STATECHANGED, &MainFrame::OnStateChanged)
        ON_MESSAGE(WM_DPICHANGED, &MainFrame::OnDpiChanged)
        ON_COMMAND(ID_FILE_NEW_TWO_WAY, &MainFrame::OnNewTwoWay)
        ON_COMMAND(ID_FILE_NEW_THREE_WAY, &MainFrame::OnNewThreeWay)
        ON_COMMAND(ID_FILE_NEW_FOLDERS, &MainFrame::OnNewFolders)
        ON_COMMAND(ID_FILE_OPEN_PATCH, &MainFrame::OnOpenPatch)
        ON_COMMAND(ID_FILE_OPEN_ARCHIVE, &MainFrame::OnOpenArchive)
        ON_COMMAND(ID_FILE_PAGE_SETUP_MF, &MainFrame::OnPageSetup)
        ON_COMMAND(ID_FILE_CLOSE_TAB, &MainFrame::OnCloseTab)
        ON_UPDATE_COMMAND_UI(ID_FILE_CLOSE_TAB, &MainFrame::OnUpdateHasComparison)
        ON_COMMAND(ID_VIEW_NEXT_TAB, &MainFrame::OnNextTab)
        ON_COMMAND(ID_VIEW_PREV_TAB, &MainFrame::OnPrevTab)
        ON_UPDATE_COMMAND_UI(ID_VIEW_NEXT_TAB, &MainFrame::OnUpdateTabs)
        ON_UPDATE_COMMAND_UI(ID_VIEW_PREV_TAB, &MainFrame::OnUpdateTabs)
        ON_COMMAND_RANGE(ID_OPT_WS_COMPARE, ID_VIEW_LINE_DETAIL, &MainFrame::OnOption)
        ON_UPDATE_COMMAND_UI_RANGE(ID_OPT_WS_COMPARE, ID_VIEW_LINE_DETAIL, &MainFrame::OnUpdateOption)
        ON_COMMAND(ID_OPT_OPTIONS, &MainFrame::OnOptions)
        ON_COMMAND(ID_EDIT_FIND_TEXT, &MainFrame::OnFind)
        ON_COMMAND(ID_EDIT_REPLACE_TEXT, &MainFrame::OnReplace)
        ON_COMMAND(ID_EDIT_FIND_NEXT_TEXT, &MainFrame::OnFindNext)
        ON_COMMAND(ID_EDIT_FIND_PREV_TEXT, &MainFrame::OnFindPrev)
        ON_UPDATE_COMMAND_UI(ID_EDIT_FIND_TEXT, &MainFrame::OnUpdateHasComparison)
        ON_UPDATE_COMMAND_UI(ID_EDIT_REPLACE_TEXT, &MainFrame::OnUpdateHasComparison)
        ON_UPDATE_COMMAND_UI(ID_EDIT_FIND_NEXT_TEXT, &MainFrame::OnUpdateHasComparison)
        ON_UPDATE_COMMAND_UI(ID_EDIT_FIND_PREV_TEXT, &MainFrame::OnUpdateHasComparison)
        ON_UPDATE_COMMAND_UI(ID_INDICATOR_ENCODING, &MainFrame::OnUpdateIndicatorEncoding)
        ON_UPDATE_COMMAND_UI(ID_INDICATOR_CHANGES, &MainFrame::OnUpdateIndicatorChanges)
        ON_UPDATE_COMMAND_UI(ID_INDICATOR_POSITION, &MainFrame::OnUpdateIndicatorPosition)
    END_MESSAGE_MAP()

    MainFrame::MainFrame() : m_find([this]() { return ActiveText(); })
    {
    }

    MainFrame::~MainFrame() = default;

    BOOL MainFrame::PreCreateWindow(CREATESTRUCT& cs)
    {
        if (!CFrameWnd::PreCreateWindow(cs))
            return FALSE;
        cs.dwExStyle &= ~WS_EX_CLIENTEDGE;
        cs.style |= WS_CLIPCHILDREN;
        return TRUE;
    }

    int MainFrame::OnCreate(LPCREATESTRUCT cs)
    {
        if (CFrameWnd::OnCreate(cs) == -1)
            return -1;
        GetFonts().Create(WindowDpi(m_hWnd));
        if (!m_commandBar.Create(this) || !m_tabStrip.Create(this, this))
            return -1;
        if (!m_statusBar.Create(this) || !m_statusBar.SetIndicators(indicators, sizeof(indicators) / sizeof(indicators[0])))
            return -1;
        m_statusBar.SetPaneInfo(0, ID_SEPARATOR, SBPS_STRETCH | SBPS_NOBORDERS, 0);
        const CString welcomeClass = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_HAND));
        m_welcome.Create(welcomeClass, L"Welcome", WS_CHILD | WS_VISIBLE, CRect(), this, AFX_IDW_PANE_FIRST);
        m_find.Create(this);
        DragAcceptFiles(TRUE);
        RefreshTabs();
        return 0;
    }

    TabWindow* MainFrame::Active() const
    {
        return m_active >= 0 && m_active < static_cast<int>(m_compares.size()) ? m_compares[static_cast<size_t>(m_active)].get() : nullptr;
    }

    CompareWnd* MainFrame::ActiveText() const
    {
        TabWindow* tab = Active();
        return tab ? tab->TextComparison() : nullptr;
    }

    BOOL MainFrame::OnCmdMsg(UINT id, int code, void* extra, AFX_CMDHANDLERINFO* handlerInfo)
    {
        // The active comparison handles its commands first.
        if (TabWindow* tab = Active())
        {
            if (tab->OnCmdMsg(id, code, extra, handlerInfo))
                return TRUE;
        }
        return CFrameWnd::OnCmdMsg(id, code, extra, handlerInfo);
    }

    void MainFrame::OnSetFocus(CWnd*)
    {
        if (TabWindow* tab = Active())
            tab->SetFocus();
    }

    bool MainFrame::OpenComparison(const std::vector<std::wstring>& paths)
    {
        // Folders (or archives, compared as folders) on every side make a folder comparison; a mixture is refused.
        int folders = 0, archives = 0, documents = 0;
        for (const std::wstring& path : paths)
        {
            const DWORD attributes = path.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(path.c_str());
            const bool folder = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
            folders += folder ? 1 : 0;
            archives += attributes != INVALID_FILE_ATTRIBUTES && !folder && IsPackedArchiveName(path) ? 1 : 0;
            documents += !path.empty() && IsDocumentFileName(path) ? 1 : 0;
        }
        if (folders > 0 || archives == static_cast<int>(paths.size()))
        {
            if (folders + archives != static_cast<int>(paths.size()))
            {
                AfxMessageBox(L"Compare folders (or archives) with folders or archives, and files with files.", MB_ICONINFORMATION);
                return false;
            }
            auto folder = std::make_unique<FolderCompareWnd>();
            if (!folder->Create(this, kInactiveCompareId))
                return false;
            folder->Open(paths);
            AddTab(std::move(folder));
            return true;
        }

        // Word, Excel, PowerPoint, OpenDocument and RTF files are compared by their text.
        if (documents == static_cast<int>(paths.size()))
            return OpenDocuments(paths);

        // Images and other binary files get their own kinds of comparison.
        bool images = paths.size() == 2, binary = false;
        for (const std::wstring& path : paths)
        {
            images = images && !path.empty() && IsImageFileName(path);
            if (!path.empty() && !binary)
            {
                std::string head;
                std::wstring error;
                binary = ReadFileBytes(path, head, error) && LooksBinary(head.substr(0, 65536));
            }
        }
        if (images || binary)
        {
            if (paths.size() != 2 || paths[0].empty() || paths[1].empty())
            {
                AfxMessageBox(L"Binary files and images can be compared two at a time.", MB_ICONINFORMATION);
                return false;
            }
            CString error;
            if (images)
            {
                auto image = std::make_unique<ImageCompareWnd>();
                if (image->Create(this, kInactiveCompareId) && image->Open(paths[0], paths[1], error))
                {
                    AddTab(std::move(image));
                    return true;
                }
                image->DestroyWindow();
                if (!binary)
                {
                    AfxMessageBox(error + L"\n\nComparing the files byte by byte instead.", MB_ICONINFORMATION);
                }
            }
            auto bytes = std::make_unique<BinaryCompareWnd>();
            error.Empty();
            if (!bytes->Create(this, kInactiveCompareId) || !bytes->Open(paths[0], paths[1], error))
            {
                bytes->DestroyWindow();
                AfxMessageBox(error, MB_ICONERROR);
                return false;
            }
            AddTab(std::move(bytes));
            return true;
        }

        auto compare = std::make_unique<CompareWnd>();
        if (!compare->Create(this, kInactiveCompareId))
            return false;
        compare->SetViewSettings(App().viewSettings);
        compare->Doc().SetOptions(App().compareOptions);
        CString error;
        if (!compare->OpenFiles(paths, error))
        {
            compare->DestroyWindow();
            AfxMessageBox(error, MB_ICONERROR);
            return false;
        }
        AddTab(std::move(compare));
        return true;
    }

    bool MainFrame::OpenPatch(const std::wstring& path)
    {
        auto patch = std::make_unique<PatchWnd>();
        CString error;
        if (!patch->Create(this, kInactiveCompareId) || !patch->Open(path, error))
        {
            patch->DestroyWindow();
            AfxMessageBox(error, MB_ICONERROR);
            return false;
        }
        AddTab(std::move(patch));
        return true;
    }

    bool MainFrame::OpenTexts(const std::vector<std::wstring>& texts, const std::vector<std::wstring>& titles)
    {
        auto compare = std::make_unique<CompareWnd>();
        if (!compare->Create(this, kInactiveCompareId))
            return false;
        compare->SetViewSettings(App().viewSettings);
        compare->Doc().SetOptions(App().compareOptions);
        CString error;
        compare->OpenFiles(std::vector<std::wstring>(texts.size()), error);
        for (size_t pane = 0; pane < texts.size(); ++pane)
        {
            compare->Doc().SetText(static_cast<int>(pane), texts[pane], titles[pane]);
            compare->Doc().Buffer(static_cast<int>(pane)).readOnly = true;
        }
        compare->OnTextChanged();
        AddTab(std::move(compare));
        return true;
    }

    bool MainFrame::OpenArchive(const std::wstring& path)
    {
        std::wstring text, error;
        CWaitCursor wait;
        if (!ReadArchiveFile(path, text, error))
        {
            AfxMessageBox((L"Cannot open " + path + L":\n" + error).c_str(), MB_ICONERROR);
            return false;
        }
        switch (ArchiveKindOf(text))
        {
        case ArchiveKind::Files:
        {
            std::unique_ptr<Comparison> doc = LoadComparisonArchive(text, error);
            if (!doc)
                break;
            auto compare = std::make_unique<CompareWnd>();
            if (!compare->Create(this, kInactiveCompareId))
                return false;
            compare->SetViewSettings(App().viewSettings);
            compare->OpenArchive(std::move(doc));
            AddTab(std::move(compare));
            return true;
        }
        case ArchiveKind::Folders:
        {
            std::unique_ptr<FolderComparison> results = LoadFolderArchive(text, error);
            if (!results)
                break;
            auto folder = std::make_unique<FolderCompareWnd>();
            if (!folder->Create(this, kInactiveCompareId))
                return false;
            folder->OpenArchive(std::move(results));
            AddTab(std::move(folder));
            return true;
        }
        default:
            error = L"This is not a MergeFiles comparison archive.";
            break;
        }
        AfxMessageBox((L"Cannot open " + path + L":\n" + error).c_str(), MB_ICONERROR);
        return false;
    }

    void MainFrame::OnOpenArchive()
    {
        CFileDialog dialog(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, kArchiveFilter, this);
        if (dialog.DoModal() == IDOK)
            OpenArchive(std::wstring(dialog.GetPathName()));
    }

    void MainFrame::OnPageSetup()
    {
        EditPageSetup(this);
    }

    bool MainFrame::OpenDocuments(const std::vector<std::wstring>& paths)
    {
        std::vector<std::wstring> texts, titles;
        CWaitCursor wait;
        for (const std::wstring& path : paths)
        {
            std::wstring text, error;
            if (!ReadDocumentText(path, text, error))
            {
                AfxMessageBox((L"Cannot read the text of " + path + L":\n" + error).c_str(), MB_ICONERROR);
                return false;
            }
            texts.push_back(std::move(text));
            const size_t slash = path.find_last_of(L"\\/");
            titles.push_back((slash == std::wstring::npos ? path : path.substr(slash + 1)) + L" (text)");
        }
        if (!OpenTexts(texts, titles))
            return false;
        SetMessageText(L"Documents are compared by their text (read-only).");
        return true;
    }

    void MainFrame::OnOpenPatch()
    {
        CFileDialog dialog(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY, L"Patch files (*.patch;*.diff)|*.patch;*.diff|All files (*.*)|*.*||", this);
        if (dialog.DoModal() == IDOK)
            OpenPatch(std::wstring(dialog.GetPathName()));
    }

    void MainFrame::AddTab(std::unique_ptr<TabWindow> tab)
    {
        m_compares.push_back(std::move(tab));
        ActivateTab(static_cast<int>(m_compares.size()) - 1);
        Active()->OnShown();
    }

    void MainFrame::ActivateTab(int index)
    {
        if (TabWindow* old = Active())
        {
            old->ShowWindow(SW_HIDE);
            old->SetDlgCtrlID(static_cast<int>(kInactiveCompareId));
        }
        m_active = index;
        if (TabWindow* compare = Active())
        {
            m_welcome.ShowWindow(SW_HIDE);
            m_welcome.SetDlgCtrlID(static_cast<int>(kInactiveWelcomeId));
            compare->SetDlgCtrlID(AFX_IDW_PANE_FIRST);
            compare->ShowWindow(SW_SHOW);
            RecalcLayout();
            compare->SetFocus();
        }
        else
        {
            m_welcome.SetDlgCtrlID(AFX_IDW_PANE_FIRST);
            m_welcome.ShowWindow(SW_SHOW);
            RecalcLayout();
        }
        RefreshTabs();
    }

    void MainFrame::RefreshTabs()
    {
        std::vector<TabItem> items;
        for (const auto& compare : m_compares)
            items.push_back(compare->GetTabItem());
        m_tabStrip.SetTabs(items, m_active);
        CString title = L"MergeFiles";
        if (TabWindow* compare = Active())
            title = compare->GetTabItem().title + (compare->IsModified() ? L" \x25CF" : L"") + L" - MergeFiles";
        CString current;
        GetWindowText(current);
        if (current != title)
            SetWindowText(title);
    }

    LRESULT MainFrame::OnStateChanged(WPARAM, LPARAM)
    {
        RefreshTabs();
        m_statusBar.OnUpdateCmdUI(this, FALSE);
        return 0;
    }

    void MainFrame::OnTabActivate(int index)
    {
        if (index != m_active)
            ActivateTab(index);
    }

    void MainFrame::OnTabClose(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_compares.size()))
            return;
        if (!m_compares[static_cast<size_t>(index)]->QueryClose())
            return;
        const bool wasActive = index == m_active;
        if (wasActive)
            m_active = -1;
        m_compares[static_cast<size_t>(index)]->DestroyWindow();
        m_compares.erase(m_compares.begin() + index);
        int next = m_active;
        if (wasActive)
            next = (std::min)(index, static_cast<int>(m_compares.size()) - 1);
        else if (m_active > index)
            next = m_active - 1;
        m_active = -1;
        ActivateTab(next);
    }

    void MainFrame::OnClose()
    {
        for (size_t i = 0; i < m_compares.size(); ++i)
        {
            if (m_compares[i]->IsModified())
                ActivateTab(static_cast<int>(i));
            if (!m_compares[i]->QueryClose())
                return;
        }
        WINDOWPLACEMENT placement = { sizeof(placement) };
        if (GetWindowPlacement(&placement))
            AfxGetApp()->WriteProfileBinary(L"Window", L"Placement", reinterpret_cast<LPBYTE>(&placement), sizeof(placement));
        if (m_find.m_hWnd)
            m_find.DestroyWindow();
        CFrameWnd::OnClose();
    }

    void MainFrame::OnDropFiles(HDROP drop)
    {
        std::vector<std::wstring> files;
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i)
        {
            wchar_t path[MAX_PATH * 4] = {};
            DragQueryFileW(drop, i, path, static_cast<UINT>(std::size(path)));
            files.emplace_back(path);
        }
        POINT point = {};
        DragQueryPoint(drop, &point);
        DragFinish(drop);
        ClientToScreen(&point);
        SetForegroundWindow();

        bool anyFolder = false;
        for (const std::wstring& file : files)
        {
            const DWORD attributes = GetFileAttributesW(file.c_str());
            anyFolder = anyFolder || (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY));
        }
        if (files.size() == 1 && IsPatchFileName(files[0]))
        {
            OpenPatch(files[0]);
            return;
        }
        if (files.size() == 1 && IsArchiveFileName(files[0]))
        {
            OpenArchive(files[0]);
            return;
        }
        if (files.size() == 1 && anyFolder)
        {
            AfxGetApp()->WriteProfileString(L"Recent", L"LeftFolder", files[0].c_str());
            NewComparison(false, true);
            return;
        }
        if (files.size() == 1)
        {
            // One file dropped on a pane replaces that pane's file; elsewhere it starts a comparison.
            CompareWnd* compare = ActiveText();
            const int pane = compare ? compare->PaneFromScreenPoint(point) : -1;
            if (pane >= 0)
            {
                if (compare->Doc().IsModified(pane) && AfxMessageBox(L"Discard the changes in this pane and open the dropped file?", MB_YESNO | MB_ICONQUESTION) != IDYES)
                    return;
                CString error;
                if (!compare->LoadIntoPane(pane, files[0], error))
                    AfxMessageBox(error, MB_ICONERROR);
                return;
            }
            AfxGetApp()->WriteProfileString(L"Recent", L"Left", files[0].c_str());
            NewComparison(false);
            return;
        }
        if (files.size() == 2 || files.size() == 3)
        {
            OpenComparison(files);
            return;
        }
        AfxMessageBox(L"Drop two files (or folders) for a two-way comparison, or three (left, ancestor, right) for a three-way comparison.", MB_ICONINFORMATION);
    }

    LRESULT MainFrame::OnDpiChanged(WPARAM wParam, LPARAM lParam)
    {
        GetFonts().Create(HIWORD(wParam));
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        for (auto& compare : m_compares)
            compare->UpdateMetrics();
        RecalcLayout();
        RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
        return 0;
    }

    void MainFrame::NewComparison(bool threeWay, bool folders)
    {
        NewComparisonDlg dialog(threeWay, folders, this);
        if (dialog.DoModal() == IDOK)
            OpenComparison(dialog.paths);
    }

    void MainFrame::OnNewTwoWay()
    {
        NewComparison(false);
    }

    void MainFrame::OnNewThreeWay()
    {
        NewComparison(true);
    }

    void MainFrame::OnNewFolders()
    {
        NewComparison(false, true);
    }

    void MainFrame::OnCloseTab()
    {
        OnTabClose(m_active);
    }

    void MainFrame::OnUpdateHasComparison(CCmdUI* ui)
    {
        ui->Enable(Active() != nullptr);
    }

    void MainFrame::OnNextTab()
    {
        if (m_compares.size() > 1)
            ActivateTab((m_active + 1) % static_cast<int>(m_compares.size()));
    }

    void MainFrame::OnPrevTab()
    {
        if (m_compares.size() > 1)
            ActivateTab((m_active + static_cast<int>(m_compares.size()) - 1) % static_cast<int>(m_compares.size()));
    }

    void MainFrame::OnUpdateTabs(CCmdUI* ui)
    {
        ui->Enable(m_compares.size() > 1);
    }

    void MainFrame::ApplySettings()
    {
        for (auto& tab : m_compares)
            tab->ApplySettings();
        App().SaveSettings();
    }

    void MainFrame::OnOptions()
    {
        OptionsData data;
        data.compare = App().compareOptions;
        data.view = App().viewSettings;
        data.folders = App().folderOptions;
        data.imageTolerance = App().imageTolerance;
        data.fontFace = GetFonts().CodeFace();
        data.fontSize = GetFonts().CodePointSize();
        data.theme = GetTheme();
        if (!ShowOptionsDialog(this, data))
            return;
        App().compareOptions = data.compare;
        App().viewSettings = data.view;
        App().folderOptions = data.folders;
        App().imageTolerance = data.imageTolerance;
        EditableTheme() = data.theme;
        if (data.fontFace != GetFonts().CodeFace() || data.fontSize != GetFonts().CodePointSize())
        {
            GetFonts().SetCodeFont(data.fontFace, data.fontSize);
            GetFonts().Create(WindowDpi(m_hWnd));
            for (auto& compare : m_compares)
                compare->UpdateMetrics();
            RecalcLayout();
        }
        ApplySettings();
        RedrawWindow(nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }

    void MainFrame::OnOption(UINT id)
    {
        CompareOptions& options = App().compareOptions;
        ViewSettings& view = App().viewSettings;
        switch (id)
        {
        case ID_OPT_WS_COMPARE:
            options.whitespace = WhitespaceMode::Compare;
            break;
        case ID_OPT_WS_IGNORE_CHANGES:
            options.whitespace = WhitespaceMode::IgnoreChanges;
            break;
        case ID_OPT_WS_IGNORE_ALL:
            options.whitespace = WhitespaceMode::IgnoreAll;
            break;
        case ID_OPT_IGNORE_CASE:
            options.ignoreCase = !options.ignoreCase;
            break;
        case ID_OPT_IGNORE_EOL:
            options.ignoreEol = !options.ignoreEol;
            break;
        case ID_OPT_SYNTAX:
            view.syntax = !view.syntax;
            break;
        case ID_OPT_SHOW_WHITESPACE:
            view.showWhitespace = !view.showWhitespace;
            break;
        case ID_OPT_IGNORE_BLANK:
            options.ignoreBlankLines = !options.ignoreBlankLines;
            break;
        case ID_VIEW_WORD_WRAP:
            view.wordWrap = !view.wordWrap;
            break;
        case ID_VIEW_PADDING:
            view.verticalPadding = !view.verticalPadding;
            break;
        case ID_VIEW_LINE_DETAIL:
            view.lineDetail = !view.lineDetail;
            break;
        default:
            return;
        }
        ApplySettings();
    }

    void MainFrame::OnUpdateOption(CCmdUI* ui)
    {
        const CompareOptions& options = App().compareOptions;
        const ViewSettings& view = App().viewSettings;
        ui->Enable(TRUE);
        switch (ui->m_nID)
        {
        case ID_OPT_WS_COMPARE:
            ui->SetRadio(options.whitespace == WhitespaceMode::Compare);
            break;
        case ID_OPT_WS_IGNORE_CHANGES:
            ui->SetRadio(options.whitespace == WhitespaceMode::IgnoreChanges);
            break;
        case ID_OPT_WS_IGNORE_ALL:
            ui->SetRadio(options.whitespace == WhitespaceMode::IgnoreAll);
            break;
        case ID_OPT_IGNORE_CASE:
            ui->SetCheck(options.ignoreCase);
            break;
        case ID_OPT_IGNORE_EOL:
            ui->SetCheck(options.ignoreEol);
            break;
        case ID_OPT_SYNTAX:
            ui->SetCheck(view.syntax);
            break;
        case ID_OPT_SHOW_WHITESPACE:
            ui->SetCheck(view.showWhitespace);
            break;
        case ID_OPT_IGNORE_BLANK:
            ui->SetCheck(options.ignoreBlankLines);
            break;
        case ID_VIEW_WORD_WRAP:
            ui->SetCheck(view.wordWrap);
            break;
        case ID_VIEW_PADDING:
            ui->SetCheck(view.verticalPadding);
            break;
        case ID_VIEW_LINE_DETAIL:
            ui->SetCheck(view.lineDetail);
            break;
        default:
            break;
        }
    }

    void MainFrame::ShowFind(bool replace)
    {
        CompareWnd* compare = ActiveText();
        if (!compare)
            return;
        CString initial;
        if (PaneView* pane = compare->Pane(compare->ActivePane()))
        {
            const std::wstring selected = pane->SelectedText();
            if (!selected.empty() && selected.find_first_of(L"\r\n") == std::wstring::npos)
                initial = selected.c_str();
        }
        m_find.Show(replace, initial);
    }

    void MainFrame::OnFind()
    {
        ShowFind(false);
    }

    void MainFrame::OnReplace()
    {
        ShowFind(true);
    }

    void MainFrame::OnFindNext()
    {
        if (!m_find.FindAgain(false))
            ShowFind(false);
    }

    void MainFrame::OnFindPrev()
    {
        if (!m_find.FindAgain(true))
            ShowFind(false);
    }

    void MainFrame::OnUpdateIndicatorEncoding(CCmdUI* ui)
    {
        TabWindow* tab = Active();
        ui->Enable(tab != nullptr);
        ui->SetText(tab ? tab->StatusText(0) : CString());
    }

    void MainFrame::OnUpdateIndicatorChanges(CCmdUI* ui)
    {
        TabWindow* tab = Active();
        ui->Enable(tab != nullptr);
        ui->SetText(tab ? tab->StatusText(1) : CString());
    }

    void MainFrame::OnUpdateIndicatorPosition(CCmdUI* ui)
    {
        TabWindow* tab = Active();
        ui->Enable(tab != nullptr);
        ui->SetText(tab ? tab->StatusText(2) : CString());
    }
}
