// PaneView.h - one file of a comparison: line numbers, edit markers, bookmarks, syntax
// highlighting, difference tints with character-level highlights, merge arrow buttons, and an
// in-place editor (caret, selection, clipboard, drag and drop of text, IME).
//
// The pane scrolls by display rows. Without word wrap or vertical padding a row is a line; with
// word wrap a long line takes several rows, and with padding blank rows keep corresponding blocks
// of all panes side by side (CompareWnd::Rows computes the layout).
#pragma once

#include "framework.h"

#include "Comparison.h"
#include "InlineDiff.h"
#include "Syntax.h"

namespace mf
{
    class CompareWnd;

    class PaneView : public CWnd
    {
    public:
        PaneView(CompareWnd& owner, int pane);

        BOOL Create(CWnd* parent);
        int Pane() const
        {
            return m_pane;
        }
        void SetPane(int pane);

        // ---- geometry (rows) ----
        int LineHeight() const
        {
            return m_lineHeight;
        }
        int TopRow() const
        {
            return m_top;
        }
        int TopLine() const;     // first line (at least partly) visible
        int BottomLine() const;  // last line (at least partly) visible
        int VisibleRows() const;  // rows that fit completely
        int LineToY(int line) const;
        int ChunkTopY(int chunk) const;
        int ChunkBottomY(int chunk) const;
        // Columns available for text (the word-wrap width).
        int TextAreaColumns() const;
        void SetTopRow(int row, bool sync);
        void CenterLine(int line, bool sync);
        void UpdateMetrics();
        // After the text, the chunks or the layout changed: scroll ranges, caret, repaint.
        void Refresh();

        // ---- caret and selection ----
        TextPos Caret() const
        {
            return m_caret;
        }
        bool HasSelection() const
        {
            return m_caret != m_anchor;
        }
        void GetSelection(TextPos& from, TextPos& to) const;
        void SetCaret(TextPos pos, bool extend, bool ensureVisible = true);
        void SetSelection(TextPos anchor, TextPos caret, bool ensureVisible = true);
        void SelectAll();
        void EnsureCaretVisible(bool sync = true);
        // First and last line touched by the selection (or the caret line).
        void SelectedLines(int& first, int& last) const;
        TextPos PosFromScreen(CPoint screenPoint) const;
        // Where dragged text would be dropped (drawn as a caret); line -1 hides it.
        void SetDropPos(TextPos pos);

        // ---- editing ----
        bool CanEdit() const;
        void InsertText(const std::wstring& text, bool typing = false);
        void DeleteSelection();
        void Copy();
        void Cut();
        void Paste();
        void IndentSelection(bool outdent);
        std::wstring SelectedText() const;

    protected:
        afx_msg void OnPaint();
        afx_msg BOOL OnEraseBkgnd(CDC* dc);
        afx_msg void OnSize(UINT type, int cx, int cy);
        afx_msg void OnVScroll(UINT code, UINT pos, CScrollBar* bar);
        afx_msg void OnHScroll(UINT code, UINT pos, CScrollBar* bar);
        afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint point);
        afx_msg void OnMouseHWheel(UINT flags, short delta, CPoint point);
        afx_msg void OnLButtonDown(UINT flags, CPoint point);
        afx_msg void OnLButtonDblClk(UINT flags, CPoint point);
        afx_msg void OnLButtonUp(UINT flags, CPoint point);
        afx_msg void OnMouseMove(UINT flags, CPoint point);
        afx_msg void OnRButtonDown(UINT flags, CPoint point);
        afx_msg void OnContextMenu(CWnd* wnd, CPoint point);
        afx_msg void OnKeyDown(UINT key, UINT repeat, UINT flags);
        afx_msg void OnChar(UINT ch, UINT repeat, UINT flags);
        afx_msg void OnSetFocus(CWnd* oldWnd);
        afx_msg void OnKillFocus(CWnd* newWnd);
        afx_msg BOOL OnSetCursor(CWnd* wnd, UINT hitTest, UINT message);
        afx_msg void OnTimer(UINT_PTR id);
        afx_msg void OnCaptureChanged(CWnd* wnd);
        afx_msg LRESULT OnImeStartComposition(WPARAM wParam, LPARAM lParam);
        afx_msg UINT OnGetDlgCode();
        DECLARE_MESSAGE_MAP()

    private:
        const TextBuffer& Buffer() const;
        int TextLeft() const;  // x of column 0 (before horizontal scrolling)
        int WrapColumns() const;  // 0 when not wrapping
        int OwnRows(int line) const;
        int LineRow(int line) const;
        int TotalRows() const;
        int MaxTopRow() const;
        // The line shown on a row and its wrapped segment; false for a padding row (line is then the
        // line before it, or -1).
        bool RowToLine(int row, int& line, int& segment) const;
        int CaretRow(TextPos pos) const;
        CPoint PosToPoint(TextPos pos) const;  // top-left of the caret
        TextPos PointToPos(CPoint point) const;
        void UpdateScrollBars();
        void UpdateCaretPos();
        void EnsureSyntaxStates(int lastLine);
        void MoveCaret(UINT key, bool extend, bool word);
        int MergeButtonAt(CPoint point) const;  // chunk index or -1
        CRect MergeButtonRect(int y) const;
        int MergeTarget() const;                 // pane the arrows copy into, or -1
        bool ShowsMergeButton(const Chunk& chunk) const;
        bool InSelection(TextPos pos) const;
        void PaintLine(CDC& dc, int line, int y, int width, ChunkStyle style, const std::vector<CharRange>* ranges);

        CompareWnd& m_owner;
        int m_pane;
        int m_top = 0;      // first visible row
        int m_xOffset = 0;  // pixels scrolled horizontally (0 when wrapping)
        TextPos m_caret;
        TextPos m_anchor;
        int m_desiredCol = -1;  // display column (within the segment) kept while moving up and down
        bool m_dragging = false;       // selecting with the mouse
        bool m_dragPending = false;    // pressed inside the selection: a click, or the start of a text drag
        bool m_textDragging = false;   // dragging the selected text
        CPoint m_dragStart;
        TextPos m_dropPos = { -1, 0 };
        bool m_hasFocus = false;
        int m_lineHeight = 16;
        int m_charWidth = 8;
        int m_marginWidth = 40;
        int m_maxColumns = 0;
        unsigned m_layoutRevision = 0;

        Language m_language = Language::PlainText;
        std::vector<int> m_states;  // syntax state at the start of each line
        unsigned m_statesRevision = 0;
        std::vector<SyntaxSpan> m_spans;
    };
}
