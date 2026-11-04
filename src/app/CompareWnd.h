// CompareWnd.h - one comparison (one tab): path bars, two or three PaneViews, the linking-line
// gutters between them and the overview strips at the edges. Owns the Comparison document and
// handles the comparison, merge and editing commands for it.
#pragma once

#include "framework.h"

#include <functional>
#include <map>

#include "Comparison.h"
#include "InlineDiff.h"
#include "Search.h"
#include "TabStrip.h"
#include "TabWindow.h"

// Posted to the frame when the tab title, modified state or status bar text may have changed.
#define WM_MF_STATECHANGED (WM_APP + 1)

namespace mf
{
    class PaneView;
    class LinkGutter;
    class OverviewStrip;
    class LineDetailPanel;

    struct ViewSettings
    {
        bool syntax = true;
        bool showWhitespace = false;
        int tabSize = 4;
        bool wordWrap = false;         // wrap long lines at the pane width
        bool verticalPadding = false;  // blank rows keep corresponding blocks side by side
        bool lineDetail = false;       // panel showing the current lines one above the other
    };

    // Character-level detail of one difference chunk in one pane.
    struct ChunkDetail
    {
        int partner = -1;                          // pane the lines were compared with
        std::vector<char> paired;                  // per line of the chunk: has a corresponding line
        std::vector<int> partnerLine;              // per line: the corresponding line in `partner`, or -1
        std::vector<std::vector<CharRange>> ranges;  // per line: changed characters
    };

    // Display rows of every pane. Without word wrap or vertical padding a row is a line.
    struct RowLayout
    {
        std::vector<std::vector<int>> lineRow;   // per pane: first row of each line (one extra entry: total rows)
        std::vector<std::vector<int>> ownRows;   // per pane: rows each line's text takes (word wrap)
        std::vector<std::vector<int>> chunkRow;  // per pane: first row of each chunk (one extra entry)
        int wrapColumns = 0;                     // 0 when not wrapping
    };

    class CompareWnd : public TabWindow
    {
    public:
        CompareWnd();
        ~CompareWnd() override;

        BOOL Create(CWnd* parent, UINT id);

        Comparison& Doc()
        {
            return m_doc;
        }
        const Comparison& Doc() const
        {
            return m_doc;
        }

        // Loads 2 or 3 files (an empty path leaves that pane empty/untitled).
        bool OpenFiles(const std::vector<std::wstring>& paths, CString& error);
        // Shows a comparison loaded from an archive (its files, options, bookmarks and links).
        void OpenArchive(std::unique_ptr<Comparison> doc);
        bool LoadIntoPane(int pane, const std::wstring& path, CString& error, const TextEncoding* forced = nullptr);
        // Call once the window has its size (scrolling needs to know how many lines fit).
        void GoToFirstDifference();

        // ---- TabWindow ----
        TabItem GetTabItem() const override;
        bool IsModified() const override
        {
            return m_doc.AnyModified();
        }
        bool QueryClose() override;
        void ApplySettings() override;
        void UpdateMetrics() override;
        CString StatusText(int part) const override;
        CompareWnd* TextComparison() override
        {
            return this;
        }
        void OnShown() override
        {
            GoToFirstDifference();
        }
        const wchar_t* HelpTopic() const override;
        // Called after a pane was saved (a folder comparison refreshes the item).
        std::function<void()> onSaved;

        void SetCompareOptions(const CompareOptions& options);
        void SetViewSettings(const ViewSettings& settings);
        const ViewSettings& GetViewSettings() const
        {
            return m_view;
        }

        // ---- used by the child windows ----
        int PaneCount() const
        {
            return m_doc.PaneCount();
        }
        PaneView* Pane(int pane) const
        {
            return m_panes[static_cast<size_t>(pane)].get();
        }
        int ActivePane() const
        {
            return m_active;
        }
        void SetActivePane(int pane);
        void SyncScroll(int fromPane);
        void OnTextChanged();
        void OnCaretMoved(int pane);
        const ChunkDetail& Detail(int chunkIndex, int pane);
        int CurrentChunk() const;  // the difference at the active pane's caret, or -1
        void CopyChunk(int chunkIndex, int from, int to);
        void JumpToLine(int pane, int line);
        void ShowPaneMenu(int pane, CPoint screenPoint);
        int PaneFromScreenPoint(CPoint point) const;
        void InvalidateAll();
        const RowLayout& Rows();
        // The line of pane `to` that corresponds to `line` of pane `from`, or -1.
        int CorrespondingLine(int from, int line, int to);
        // Dragging selected text between (or within) panes.
        void UpdateTextDrag(int sourcePane, CPoint screenPoint, bool copy);
        void EndTextDrag(int sourcePane, CPoint screenPoint, bool copy, bool drop);

        // ---- status bar ----
        CString EncodingStatus() const;
        CString ChangesStatus() const;
        CString PositionStatus() const;

        // ---- find/replace (used by the Find and Replace dialog) ----
        bool FindNext(const std::wstring& what, SearchOptions options, bool selectionOnly);
        bool ReplaceCurrent(const std::wstring& what, const std::wstring& with, SearchOptions options, bool selectionOnly);
        int ReplaceAll(const std::wstring& what, const std::wstring& with, SearchOptions options, bool selectionOnly);

    protected:
        afx_msg void OnPaint();
        afx_msg BOOL OnEraseBkgnd(CDC* dc);
        afx_msg void OnSize(UINT type, int cx, int cy);
        afx_msg void OnLButtonDown(UINT flags, CPoint point);
        afx_msg void OnLButtonDblClk(UINT flags, CPoint point);
        afx_msg void OnSetFocus(CWnd* oldWnd);

        afx_msg void OnSavePane();
        afx_msg void OnSavePaneAs();
        afx_msg void OnSaveAll();
        afx_msg void OnOpenInPane();
        afx_msg void OnUpdateSavePane(CCmdUI* ui);
        afx_msg void OnUpdateSaveAll(CCmdUI* ui);
        afx_msg void OnUpdateAlways(CCmdUI* ui);
        afx_msg void OnUndo();
        afx_msg void OnRedo();
        afx_msg void OnUpdateUndo(CCmdUI* ui);
        afx_msg void OnUpdateRedo(CCmdUI* ui);
        afx_msg void OnCut();
        afx_msg void OnCopy();
        afx_msg void OnPaste();
        afx_msg void OnSelectAll();
        afx_msg void OnUpdateCut(CCmdUI* ui);
        afx_msg void OnUpdateCopy(CCmdUI* ui);
        afx_msg void OnUpdatePaste(CCmdUI* ui);
        afx_msg void OnGoToLine();
        afx_msg void OnIndent();
        afx_msg void OnOutdent();
        afx_msg void OnUpdateEditable(CCmdUI* ui);
        afx_msg void OnStart();
        afx_msg void OnUpdateStart(CCmdUI* ui);
        afx_msg void OnUpdateStop(CCmdUI* ui);
        afx_msg void OnRecompare();
        afx_msg void OnLayoutTwo();
        afx_msg void OnLayoutThree();
        afx_msg void OnUpdateLayoutTwo(CCmdUI* ui);
        afx_msg void OnUpdateLayoutThree(CCmdUI* ui);
        afx_msg void OnStatistics();
        afx_msg void OnNextChange();
        afx_msg void OnPrevChange();
        afx_msg void OnNextConflict();
        afx_msg void OnPrevConflict();
        afx_msg void OnUpdateNextChange(CCmdUI* ui);
        afx_msg void OnUpdatePrevChange(CCmdUI* ui);
        afx_msg void OnUpdateNextConflict(CCmdUI* ui);
        afx_msg void OnUpdatePrevConflict(CCmdUI* ui);
        afx_msg void OnCopyToRight();
        afx_msg void OnCopyToLeft();
        afx_msg void OnUpdateCopyToRight(CCmdUI* ui);
        afx_msg void OnUpdateCopyToLeft(CCmdUI* ui);
        afx_msg void OnAutoMerge();
        afx_msg void OnUpdateAutoMerge(CCmdUI* ui);
        afx_msg void OnMarkResolved();
        afx_msg void OnUnmarkResolved();
        afx_msg void OnReplaceRight();
        afx_msg void OnReplaceLeft();
        afx_msg void OnUpdateReplaceRight(CCmdUI* ui);
        afx_msg void OnUpdateReplaceLeft(CCmdUI* ui);
        afx_msg void OnEncoding(UINT id);
        afx_msg void OnUpdateEncoding(CCmdUI* ui);
        afx_msg void OnReopen(UINT id);
        afx_msg void OnUpdateReopen(CCmdUI* ui);
        afx_msg void OnEol(UINT id);
        afx_msg void OnUpdateEol(CCmdUI* ui);
        afx_msg void OnFormatXml();
        afx_msg void OnUpdateFormatXml(CCmdUI* ui);
        afx_msg void OnToggleBookmark();
        afx_msg void OnNextBookmark();
        afx_msg void OnPrevBookmark();
        afx_msg void OnClearBookmarks();
        afx_msg void OnUpdateBookmarks(CCmdUI* ui);
        afx_msg void OnSyncAdd();
        afx_msg void OnSyncClear();
        afx_msg void OnUpdateSyncClear(CCmdUI* ui);
        afx_msg void OnBookmarkComment();
        afx_msg void OnPrint();
        afx_msg void OnPrintPreview();
        afx_msg void OnReport();
        afx_msg void OnSaveArchive();
        DECLARE_MESSAGE_MAP()

    private:
        void CreateChildren();
        void Layout();
        CRect PathBarRect(int pane) const;
        void GoToChunk(int index);
        void Navigate(bool forward, bool conflictsOnly);
        bool CanNavigate(bool forward, bool conflictsOnly) const;
        // The panes "copy to the right/left" copies between for the current chunk; false if none.
        bool CopyPanes(bool toRight, int& from, int& to) const;
        bool SavePane(int pane, bool saveAs);
        void AfterUndoRedo(bool ok, int pane, TextPos caret);
        void NotifyFrame();

        Comparison m_doc;
        ViewSettings m_view;
        std::vector<std::unique_ptr<PaneView>> m_panes;
        std::vector<std::unique_ptr<LinkGutter>> m_gutters;
        std::unique_ptr<OverviewStrip> m_overviewLeft;
        std::unique_ptr<OverviewStrip> m_overviewRight;
        std::unique_ptr<LineDetailPanel> m_lineDetail;
        RowLayout m_rows;
        unsigned m_rowsRevision = 0;
        int m_rowsKey[5] = {};
        int m_active = 0;
        bool m_syncing = false;
        std::map<std::pair<int, int>, ChunkDetail> m_details;
        unsigned m_detailsRevision = 0;
        int m_commentLine = -1;  // the line whose bookmark comment the status bar shows
        int m_commentPane = -1;
        bool m_commentShown = false;
    };
}
