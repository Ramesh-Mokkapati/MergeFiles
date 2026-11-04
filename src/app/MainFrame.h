// MainFrame.h - the main window: menu, grouped toolbar, comparison tab strip, status bar, and one
// CompareWnd per open comparison (the active one fills the client area).
#pragma once

#include "framework.h"

#include "CommandBar.h"
#include "CompareWnd.h"
#include "TabWindow.h"
#include "Dialogs.h"
#include "TabStrip.h"

namespace mf
{
    // Shown when no comparison is open.
    class WelcomeWnd : public CWnd
    {
    protected:
        afx_msg void OnPaint();
        afx_msg void OnLButtonUp(UINT flags, CPoint point);
        DECLARE_MESSAGE_MAP()
    };

    class MainFrame : public CFrameWnd, public TabStripOwner
    {
    public:
        MainFrame();
        ~MainFrame() override;

        // Opens a new tab comparing 2 or 3 files, or 2 or 3 folders.
        bool OpenComparison(const std::vector<std::wstring>& paths);
        TabWindow* Active() const;
        bool OpenPatch(const std::wstring& path);
        // A comparison archive (*.mfcmp) of files or folders.
        bool OpenArchive(const std::wstring& path);
        // Word, Excel, PowerPoint, OpenDocument or RTF files, compared by their text (read-only).
        bool OpenDocuments(const std::vector<std::wstring>& paths);
        // A read-only comparison of texts that are not files (e.g. formatted XML).
        bool OpenTexts(const std::vector<std::wstring>& texts, const std::vector<std::wstring>& titles);
        // The file comparison of the active tab (a folder tab's comparison below the folders), or nullptr.
        CompareWnd* ActiveText() const;

        void OnTabActivate(int index) override;
        void OnTabClose(int index) override;

        BOOL OnCmdMsg(UINT id, int code, void* extra, AFX_CMDHANDLERINFO* handlerInfo) override;

    protected:
        BOOL PreCreateWindow(CREATESTRUCT& cs) override;
        afx_msg int OnCreate(LPCREATESTRUCT cs);
        afx_msg void OnClose();
        afx_msg void OnSetFocus(CWnd* oldWnd);
        afx_msg void OnDropFiles(HDROP drop);
        afx_msg LRESULT OnStateChanged(WPARAM, LPARAM);
        afx_msg LRESULT OnDpiChanged(WPARAM wParam, LPARAM lParam);
        afx_msg void OnNewTwoWay();
        afx_msg void OnNewThreeWay();
        afx_msg void OnNewFolders();
        afx_msg void OnOpenPatch();
        afx_msg void OnOpenArchive();
        afx_msg void OnPageSetup();
        afx_msg void OnCloseTab();
        afx_msg void OnUpdateHasComparison(CCmdUI* ui);
        afx_msg void OnNextTab();
        afx_msg void OnPrevTab();
        afx_msg void OnUpdateTabs(CCmdUI* ui);
        afx_msg void OnOption(UINT id);
        afx_msg void OnUpdateOption(CCmdUI* ui);
        afx_msg void OnOptions();
        afx_msg void OnFind();
        afx_msg void OnReplace();
        afx_msg void OnFindNext();
        afx_msg void OnFindPrev();
        afx_msg void OnUpdateIndicatorEncoding(CCmdUI* ui);
        afx_msg void OnUpdateIndicatorChanges(CCmdUI* ui);
        afx_msg void OnUpdateIndicatorPosition(CCmdUI* ui);
        DECLARE_MESSAGE_MAP()

    private:
        void NewComparison(bool threeWay, bool folders = false);
        void AddTab(std::unique_ptr<TabWindow> tab);
        void ActivateTab(int index);
        void RefreshTabs();
        void ApplySettings();
        void ShowFind(bool replace);

        std::vector<std::unique_ptr<TabWindow>> m_compares;
        int m_active = -1;
        CommandBar m_commandBar;
        TabStrip m_tabStrip;
        CStatusBar m_statusBar;
        WelcomeWnd m_welcome;
        FindReplaceDlg m_find;
    };
}
