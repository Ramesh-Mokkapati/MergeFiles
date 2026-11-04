// TabWindow.h - what the main frame needs from the window of one tab: a file comparison
// (CompareWnd) or a folder comparison (FolderCompareWnd).
#pragma once

#include "framework.h"

#include "TabStrip.h"

namespace mf
{
    class CompareWnd;

    class TabWindow : public CWnd
    {
    public:
        virtual TabItem GetTabItem() const = 0;
        virtual bool IsModified() const = 0;
        // Asks whether to save modified files (or stop a running comparison); false to cancel.
        virtual bool QueryClose() = 0;
        // Re-reads the options (comparison rules, display settings) from the application.
        virtual void ApplySettings() = 0;
        // After a font or DPI change.
        virtual void UpdateMetrics() = 0;
        // Status bar texts: 0 = encoding/mode, 1 = counts, 2 = position/current item.
        virtual CString StatusText(int part) const = 0;
        // The file comparison that text commands (find, go to line, pane drop) apply to, or nullptr.
        virtual CompareWnd* TextComparison() = 0;
        // Called once the tab is shown with its final size.
        virtual void OnShown()
        {
        }
        // The help topic (an anchor of MergeFiles.html) for F1 while this tab is active.
        virtual const wchar_t* HelpTopic() const
        {
            return L"overview";
        }
    };
}
