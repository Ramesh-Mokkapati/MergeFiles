// ImageCompareWnd.h - pixel comparison of two images (one tab): left, right and a differences
// panel side by side, sharing zoom and position; differing areas are outlined and can be visited
// with next/previous change.
#pragma once

#include "framework.h"

#include "ImageDiff.h"
#include "TabWindow.h"

namespace mf
{
    class ImageCompareWnd;

    class ImagePanel : public CWnd
    {
    public:
        ImagePanel(ImageCompareWnd& owner, int which) : m_owner(owner), m_which(which)
        {
        }
        BOOL Create(CWnd* parent);

    protected:
        afx_msg void OnPaint();
        afx_msg BOOL OnEraseBkgnd(CDC* dc);
        afx_msg BOOL OnMouseWheel(UINT flags, short delta, CPoint point);
        afx_msg void OnLButtonDown(UINT flags, CPoint point);
        afx_msg void OnLButtonUp(UINT flags, CPoint point);
        afx_msg void OnMouseMove(UINT flags, CPoint point);
        afx_msg void OnMouseLeave();
        DECLARE_MESSAGE_MAP()

    private:
        ImageCompareWnd& m_owner;
        int m_which;  // 0 left, 1 right, 2 differences
        bool m_dragging = false;
        bool m_tracking = false;
        CPoint m_last;
    };

    class ImageCompareWnd : public TabWindow
    {
    public:
        ImageCompareWnd();
        ~ImageCompareWnd() override;

        BOOL Create(CWnd* parent, UINT id);
        bool Open(const std::wstring& left, const std::wstring& right, CString& error);

        // ---- TabWindow ----
        TabItem GetTabItem() const override;
        bool IsModified() const override
        {
            return false;
        }
        bool QueryClose() override
        {
            return true;
        }
        void ApplySettings() override;
        void UpdateMetrics() override
        {
            Layout();
        }
        CString StatusText(int part) const override;
        const wchar_t* HelpTopic() const override
        {
            return L"images";
        }
        CompareWnd* TextComparison() override
        {
            return nullptr;
        }
        void OnShown() override;

        // ---- used by the panels ----
        const Image& Display(int which) const
        {
            return m_display[which];
        }
        const ImageDiff& Diff() const
        {
            return m_diff;
        }
        int CurrentRegion() const
        {
            return m_current;
        }
        double Scale(const CRect& panel) const;
        // Panel point <-> image coordinates.
        CPoint ToPanel(const CRect& panel, double x, double y) const;
        void ToImage(const CRect& panel, CPoint point, double& x, double& y) const;
        void Pan(double dx, double dy);
        void ZoomAt(double factor, const CRect& panel, CPoint point);
        void OnHover(bool inside, double x, double y);
        void InvalidatePanels();

    protected:
        afx_msg void OnPaint();
        afx_msg BOOL OnEraseBkgnd(CDC* dc);
        afx_msg void OnSize(UINT type, int cx, int cy);
        afx_msg void OnNextChange();
        afx_msg void OnPrevChange();
        afx_msg void OnUpdateNextChange(CCmdUI* ui);
        afx_msg void OnUpdatePrevChange(CCmdUI* ui);
        afx_msg void OnZoomIn();
        afx_msg void OnZoomOut();
        afx_msg void OnZoomFit();
        afx_msg void OnZoomActual();
        afx_msg void OnUpdateZoomFit(CCmdUI* ui);
        afx_msg void OnReload();
        afx_msg void OnStatistics();
        afx_msg void OnUpdateAlways(CCmdUI* ui);
        DECLARE_MESSAGE_MAP()

    private:
        void Compare();
        void Layout();
        void GoToRegion(int index);
        void SetZoom(double zoom);
        void NotifyFrame();

        std::wstring m_paths[2];
        Image m_images[2];
        Image m_display[3];  // composited on white; [2] = differences
        ImageDiff m_diff;
        int m_tolerance = 0;
        int m_current = -1;
        std::unique_ptr<ImagePanel> m_panels[3];
        bool m_fit = true;
        double m_zoom = 1.0;
        double m_centerX = 0, m_centerY = 0;
        bool m_hover = false;
        int m_hoverX = 0, m_hoverY = 0;
    };
}
