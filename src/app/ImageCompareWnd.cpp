// ImageCompareWnd.cpp - image panels and the image comparison tab.
#include "ImageCompareWnd.h"
#include "CompareWnd.h"

#include <cmath>

#include "Dialogs.h"
#include "MergeFilesApp.h"
#include "Theme.h"
#include "resource.h"

namespace mf
{
    namespace
    {
        std::wstring LeafName(const std::wstring& path)
        {
            const size_t slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? path : path.substr(slash + 1);
        }

        // Transparent pixels shown over white, so StretchDIBits can draw them.
        Image OnWhite(const Image& image)
        {
            Image result = image;
            for (uint32_t& p : result.pixels)
            {
                const uint32_t alpha = p >> 24;
                if (alpha == 255)
                    continue;
                uint32_t out = 0xFF000000;
                for (int shift = 0; shift < 24; shift += 8)
                {
                    const uint32_t channel = (p >> shift) & 0xFF;
                    out |= ((channel * alpha + 255 * (255 - alpha)) / 255) << shift;
                }
                p = out;
            }
            return result;
        }

        CString Color(uint32_t p)
        {
            CString text;
            if ((p >> 24) == 255)
                text.Format(L"#%02X%02X%02X", (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF);
            else
                text.Format(L"#%02X%02X%02X alpha %u", (p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF, p >> 24);
            return text;
        }
    }

    // ---- ImagePanel ----

    BEGIN_MESSAGE_MAP(ImagePanel, CWnd)
        ON_WM_PAINT()
        ON_WM_ERASEBKGND()
        ON_WM_MOUSEWHEEL()
        ON_WM_LBUTTONDOWN()
        ON_WM_LBUTTONUP()
        ON_WM_MOUSEMOVE()
        ON_WM_MOUSELEAVE()
    END_MESSAGE_MAP()

    BOOL ImagePanel::Create(CWnd* parent)
    {
        const CString className = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_SIZEALL));
        return CWnd::Create(className, L"Image", WS_CHILD | WS_VISIBLE, CRect(), parent, 800 + m_which);
    }

    BOOL ImagePanel::OnEraseBkgnd(CDC*)
    {
        return TRUE;
    }

    void ImagePanel::OnPaint()
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
        dc.FillSolidRect(&client, theme.gutterBack);

        const Image& image = m_owner.Display(m_which);
        if (!image.Empty())
        {
            const CPoint topLeft = m_owner.ToPanel(client, 0, 0);
            const CPoint bottomRight = m_owner.ToPanel(client, image.width, image.height);
            BITMAPINFO info = {};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = image.width;
            info.bmiHeader.biHeight = -image.height;  // top-down rows
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            // Smooth when shrinking, sharp pixels when magnifying.
            dc.SetStretchBltMode(m_owner.Scale(client) < 1.0 ? HALFTONE : COLORONCOLOR);
            SetBrushOrgEx(dc.m_hDC, 0, 0, nullptr);
            StretchDIBits(dc.m_hDC, topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y, 0, 0, image.width, image.height,
                image.pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
            CBrush frame(theme.chromeBorder);
            CRect border(topLeft, bottomRight);
            border.InflateRect(1, 1);
            dc.FrameRect(&border, &frame);
        }

        // Outline the differing areas.
        const ImageDiff& diff = m_owner.Diff();
        for (size_t r = 0; r < diff.regions.size(); ++r)
        {
            const ImageDiff::Region& region = diff.regions[r];
            const bool current = static_cast<int>(r) == m_owner.CurrentRegion();
            CRect box(m_owner.ToPanel(client, region.left, region.top), m_owner.ToPanel(client, region.right, region.bottom));
            box.InflateRect(2, 2);
            CPen pen(current ? PS_SOLID : PS_DOT, current ? GetFonts().Scale(2) : 1, theme.conflictLine);
            CPen* oldPen = dc.SelectObject(&pen);
            CBrush* oldBrush = static_cast<CBrush*>(dc.SelectStockObject(NULL_BRUSH));
            dc.SetBkMode(TRANSPARENT);
            dc.Rectangle(&box);
            dc.SelectObject(oldPen);
            dc.SelectObject(oldBrush);
        }

        // Caption.
        static const wchar_t* names[] = { L"Left", L"Right", L"Differences" };
        CString caption = names[m_which];
        if (!image.Empty())
        {
            CString size;
            size.Format(L"  %d \x00D7 %d", image.width, image.height);
            if (m_which < 2)
                caption += size;
        }
        CFont* oldFont = dc.SelectObject(&GetFonts().uiSmall);
        CSize extent = dc.GetTextExtent(caption);
        CRect label(GetFonts().Scale(6), GetFonts().Scale(6), GetFonts().Scale(18) + extent.cx, GetFonts().Scale(10) + extent.cy);
        dc.FillSolidRect(&label, Blend(theme.pathBarBack, RGB(255, 255, 255), 30));
        dc.SetTextColor(m_which == 2 ? theme.conflictLine : theme.chromeText);
        dc.SetBkMode(TRANSPARENT);
        dc.DrawText(caption, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        dc.SelectObject(oldFont);

        paint.BitBlt(0, 0, client.Width(), client.Height(), &dc, 0, 0, SRCCOPY);
        dc.SelectObject(oldBitmap);
    }

    BOOL ImagePanel::OnMouseWheel(UINT flags, short delta, CPoint point)
    {
        CRect client;
        GetClientRect(&client);
        ScreenToClient(&point);
        if (flags & MK_CONTROL)
        {
            m_owner.ZoomAt(delta > 0 ? 1.25 : 0.8, client, point);
            return TRUE;
        }
        const double scale = m_owner.Scale(client);
        const double step = client.Height() / 6.0 / scale;
        if (flags & MK_SHIFT)
            m_owner.Pan(delta > 0 ? -step : step, 0);
        else
            m_owner.Pan(0, delta > 0 ? -step : step);
        return TRUE;
    }

    void ImagePanel::OnLButtonDown(UINT, CPoint point)
    {
        SetFocus();
        m_dragging = true;
        m_last = point;
        SetCapture();
    }

    void ImagePanel::OnLButtonUp(UINT, CPoint)
    {
        if (m_dragging)
        {
            m_dragging = false;
            ReleaseCapture();
        }
    }

    void ImagePanel::OnMouseMove(UINT, CPoint point)
    {
        CRect client;
        GetClientRect(&client);
        if (m_dragging)
        {
            const double scale = m_owner.Scale(client);
            m_owner.Pan((m_last.x - point.x) / scale, (m_last.y - point.y) / scale);
            m_last = point;
        }
        if (!m_tracking)
        {
            TRACKMOUSEEVENT track = { sizeof(track), TME_LEAVE, m_hWnd, 0 };
            m_tracking = TrackMouseEvent(&track) != FALSE;
        }
        double x = 0, y = 0;
        m_owner.ToImage(client, point, x, y);
        m_owner.OnHover(true, x, y);
    }

    void ImagePanel::OnMouseLeave()
    {
        m_tracking = false;
        m_owner.OnHover(false, 0, 0);
    }

    // ---- ImageCompareWnd ----

    BEGIN_MESSAGE_MAP(ImageCompareWnd, TabWindow)
        ON_WM_PAINT()
        ON_WM_ERASEBKGND()
        ON_WM_SIZE()
        ON_COMMAND(ID_MERGE_NEXT_CHANGE, &ImageCompareWnd::OnNextChange)
        ON_COMMAND(ID_MERGE_PREV_CHANGE, &ImageCompareWnd::OnPrevChange)
        ON_UPDATE_COMMAND_UI(ID_MERGE_NEXT_CHANGE, &ImageCompareWnd::OnUpdateNextChange)
        ON_UPDATE_COMMAND_UI(ID_MERGE_PREV_CHANGE, &ImageCompareWnd::OnUpdatePrevChange)
        ON_COMMAND(ID_VIEW_ZOOM_IN, &ImageCompareWnd::OnZoomIn)
        ON_COMMAND(ID_VIEW_ZOOM_OUT, &ImageCompareWnd::OnZoomOut)
        ON_COMMAND(ID_VIEW_ZOOM_FIT, &ImageCompareWnd::OnZoomFit)
        ON_COMMAND(ID_VIEW_ZOOM_ACTUAL, &ImageCompareWnd::OnZoomActual)
        ON_UPDATE_COMMAND_UI(ID_VIEW_ZOOM_IN, &ImageCompareWnd::OnUpdateAlways)
        ON_UPDATE_COMMAND_UI(ID_VIEW_ZOOM_OUT, &ImageCompareWnd::OnUpdateAlways)
        ON_UPDATE_COMMAND_UI(ID_VIEW_ZOOM_FIT, &ImageCompareWnd::OnUpdateZoomFit)
        ON_UPDATE_COMMAND_UI(ID_VIEW_ZOOM_ACTUAL, &ImageCompareWnd::OnUpdateAlways)
        ON_COMMAND(ID_VIEW_RECOMPARE, &ImageCompareWnd::OnReload)
        ON_COMMAND(ID_VIEW_START, &ImageCompareWnd::OnReload)
        ON_UPDATE_COMMAND_UI(ID_VIEW_RECOMPARE, &ImageCompareWnd::OnUpdateAlways)
        ON_UPDATE_COMMAND_UI(ID_VIEW_START, &ImageCompareWnd::OnUpdateAlways)
        ON_COMMAND(ID_VIEW_STATISTICS, &ImageCompareWnd::OnStatistics)
        ON_UPDATE_COMMAND_UI(ID_VIEW_STATISTICS, &ImageCompareWnd::OnUpdateAlways)
    END_MESSAGE_MAP()

    ImageCompareWnd::ImageCompareWnd() = default;
    ImageCompareWnd::~ImageCompareWnd() = default;

    BOOL ImageCompareWnd::Create(CWnd* parent, UINT id)
    {
        const CString className = AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(nullptr, IDC_ARROW));
        if (!CWnd::Create(className, L"Image comparison", WS_CHILD | WS_CLIPCHILDREN, CRect(), parent, id))
            return FALSE;
        for (int which = 0; which < 3; ++which)
        {
            m_panels[which] = std::make_unique<ImagePanel>(*this, which);
            m_panels[which]->Create(this);
        }
        return TRUE;
    }

    bool ImageCompareWnd::Open(const std::wstring& left, const std::wstring& right, CString& error)
    {
        m_paths[0] = left;
        m_paths[1] = right;
        for (int side = 0; side < 2; ++side)
        {
            std::wstring message;
            if (!LoadImageFile(m_paths[side], m_images[side], message))
            {
                error.Format(L"Cannot open %s:\n%s", m_paths[side].c_str(), message.c_str());
                return false;
            }
            m_display[side] = OnWhite(m_images[side]);
        }
        m_tolerance = App().imageTolerance;
        Compare();
        return true;
    }

    void ImageCompareWnd::Compare()
    {
        m_diff = DiffImages(m_images[0], m_images[1], m_tolerance);
        m_display[2] = MakeDifferenceImage(m_display[0], m_diff);
        m_current = -1;
        m_centerX = m_diff.width / 2.0;
        m_centerY = m_diff.height / 2.0;
        InvalidatePanels();
        NotifyFrame();
    }

    void ImageCompareWnd::ApplySettings()
    {
        if (App().imageTolerance != m_tolerance)
        {
            m_tolerance = App().imageTolerance;
            Compare();
        }
    }

    void ImageCompareWnd::OnShown()
    {
        Layout();
        InvalidatePanels();
    }

    double ImageCompareWnd::Scale(const CRect& panel) const
    {
        if (!m_fit)
            return m_zoom;
        const int width = (std::max)(1, m_diff.width);
        const int height = (std::max)(1, m_diff.height);
        const double fit = (std::min)((panel.Width() - 16.0) / width, (panel.Height() - 16.0) / height);
        return (std::max)(0.01, (std::min)(fit, 16.0));
    }

    CPoint ImageCompareWnd::ToPanel(const CRect& panel, double x, double y) const
    {
        const double scale = Scale(panel);
        const double centerX = m_fit ? m_diff.width / 2.0 : m_centerX;
        const double centerY = m_fit ? m_diff.height / 2.0 : m_centerY;
        return CPoint(static_cast<int>(std::floor(panel.Width() / 2.0 + (x - centerX) * scale)), static_cast<int>(std::floor(panel.Height() / 2.0 + (y - centerY) * scale)));
    }

    void ImageCompareWnd::ToImage(const CRect& panel, CPoint point, double& x, double& y) const
    {
        const double scale = Scale(panel);
        const double centerX = m_fit ? m_diff.width / 2.0 : m_centerX;
        const double centerY = m_fit ? m_diff.height / 2.0 : m_centerY;
        x = centerX + (point.x - panel.Width() / 2.0) / scale;
        y = centerY + (point.y - panel.Height() / 2.0) / scale;
    }

    void ImageCompareWnd::SetZoom(double zoom)
    {
        if (m_fit)
        {
            // Leave "fit", centred.
            m_centerX = m_diff.width / 2.0;
            m_centerY = m_diff.height / 2.0;
            m_fit = false;
        }
        m_zoom = (std::max)(0.02, (std::min)(zoom, 64.0));
        InvalidatePanels();
        NotifyFrame();
    }

    void ImageCompareWnd::Pan(double dx, double dy)
    {
        if (m_fit)
        {
            CRect panel;
            m_panels[0]->GetClientRect(&panel);
            m_zoom = Scale(panel);
            m_centerX = m_diff.width / 2.0;
            m_centerY = m_diff.height / 2.0;
            m_fit = false;
        }
        m_centerX = (std::max)(0.0, (std::min)(m_centerX + dx, static_cast<double>(m_diff.width)));
        m_centerY = (std::max)(0.0, (std::min)(m_centerY + dy, static_cast<double>(m_diff.height)));
        InvalidatePanels();
    }

    void ImageCompareWnd::ZoomAt(double factor, const CRect& panel, CPoint point)
    {
        // Keep the image point under the cursor where it is.
        double x = 0, y = 0;
        ToImage(panel, point, x, y);
        const double scale = Scale(panel);
        if (m_fit)
        {
            m_centerX = m_diff.width / 2.0;
            m_centerY = m_diff.height / 2.0;
            m_fit = false;
        }
        m_zoom = (std::max)(0.02, (std::min)(scale * factor, 64.0));
        m_centerX = x - (point.x - panel.Width() / 2.0) / m_zoom;
        m_centerY = y - (point.y - panel.Height() / 2.0) / m_zoom;
        InvalidatePanels();
        NotifyFrame();
    }

    void ImageCompareWnd::OnHover(bool inside, double x, double y)
    {
        m_hover = inside && x >= 0 && y >= 0 && x < m_diff.width && y < m_diff.height;
        m_hoverX = static_cast<int>(x);
        m_hoverY = static_cast<int>(y);
        NotifyFrame();
    }

    void ImageCompareWnd::InvalidatePanels()
    {
        for (auto& panel : m_panels)
        {
            if (panel && panel->m_hWnd)
                panel->Invalidate(FALSE);
        }
    }

    void ImageCompareWnd::GoToRegion(int index)
    {
        if (index < 0 || index >= static_cast<int>(m_diff.regions.size()))
            return;
        m_current = index;
        const ImageDiff::Region& region = m_diff.regions[static_cast<size_t>(index)];
        CRect panel;
        m_panels[0]->GetClientRect(&panel);
        // Zoom so the area fills about half the panel (never below the fitted scale).
        const double fit = Scale(panel);
        const double wanted = (std::min)(panel.Width() * 0.5 / (std::max)(1, region.right - region.left), panel.Height() * 0.5 / (std::max)(1, region.bottom - region.top));
        m_zoom = (std::max)(fit, (std::min)(wanted, 16.0));
        m_fit = false;
        m_centerX = (region.left + region.right) / 2.0;
        m_centerY = (region.top + region.bottom) / 2.0;
        InvalidatePanels();
        NotifyFrame();
    }

    void ImageCompareWnd::OnNextChange()
    {
        GoToRegion(m_current + 1);
    }

    void ImageCompareWnd::OnPrevChange()
    {
        GoToRegion(m_current < 0 ? static_cast<int>(m_diff.regions.size()) - 1 : m_current - 1);
    }

    void ImageCompareWnd::OnUpdateNextChange(CCmdUI* ui)
    {
        ui->Enable(m_current + 1 < static_cast<int>(m_diff.regions.size()));
    }

    void ImageCompareWnd::OnUpdatePrevChange(CCmdUI* ui)
    {
        ui->Enable(!m_diff.regions.empty() && m_current != 0);
    }

    void ImageCompareWnd::OnZoomIn()
    {
        CRect panel;
        m_panels[0]->GetClientRect(&panel);
        SetZoom(Scale(panel) * 1.5);
    }

    void ImageCompareWnd::OnZoomOut()
    {
        CRect panel;
        m_panels[0]->GetClientRect(&panel);
        SetZoom(Scale(panel) / 1.5);
    }

    void ImageCompareWnd::OnZoomFit()
    {
        m_fit = true;
        m_current = -1;
        InvalidatePanels();
        NotifyFrame();
    }

    void ImageCompareWnd::OnZoomActual()
    {
        SetZoom(1.0);
    }

    void ImageCompareWnd::OnUpdateZoomFit(CCmdUI* ui)
    {
        ui->Enable(TRUE);
        ui->SetCheck(m_fit);
    }

    void ImageCompareWnd::OnUpdateAlways(CCmdUI* ui)
    {
        ui->Enable(TRUE);
    }

    void ImageCompareWnd::OnReload()
    {
        CString error;
        if (!Open(m_paths[0], m_paths[1], error))
            AfxMessageBox(error, MB_ICONERROR);
    }

    void ImageCompareWnd::OnStatistics()
    {
        CString text;
        const double total = static_cast<double>(m_diff.width) * m_diff.height;
        text.Format(L"Left: %s\r\n    %d \x00D7 %d\r\n\r\nRight: %s\r\n    %d \x00D7 %d\r\n\r\n%I64u pixels differ (%.3f%%) in %d area(s).\r\nLargest channel difference: %d\r\nColour tolerance: %d",
            m_paths[0].c_str(), m_images[0].width, m_images[0].height, m_paths[1].c_str(), m_images[1].width, m_images[1].height,
            static_cast<unsigned long long>(m_diff.differing), total > 0 ? 100.0 * static_cast<double>(m_diff.differing) / total : 0.0,
            static_cast<int>(m_diff.regions.size()), m_diff.maxDelta, m_tolerance);
        TextDlg dialog(IDD_STATISTICS, text, this);
        dialog.DoModal();
    }

    TabItem ImageCompareWnd::GetTabItem() const
    {
        TabItem item;
        item.kind = TabKind::Image;
        item.title = (LeafName(m_paths[0]) + L", " + LeafName(m_paths[1])).c_str();
        item.tooltip = (m_paths[0] + L"\n" + m_paths[1]).c_str();
        return item;
    }

    CString ImageCompareWnd::StatusText(int part) const
    {
        CString text;
        if (part == 0)
        {
            text.Format(L"Image comparison \x2022 tolerance %d", m_tolerance);
            return text;
        }
        if (part == 1)
        {
            if (m_diff.differing == 0)
                return L"Identical images";
            const double total = static_cast<double>(m_diff.width) * m_diff.height;
            text.Format(L"%I64u pixels differ (%.2f%%) \x2022 %d areas%s", static_cast<unsigned long long>(m_diff.differing),
                100.0 * static_cast<double>(m_diff.differing) / total, static_cast<int>(m_diff.regions.size()), m_diff.sameSize ? L"" : L" \x2022 sizes differ");
            return text;
        }
        if (m_hover)
        {
            auto pixel = [&](int side) -> CString {
                const Image& image = m_images[side];
                if (m_hoverX >= image.width || m_hoverY >= image.height)
                    return L"\x2014";
                return Color(image.At(m_hoverX, m_hoverY));
            };
            text.Format(L"%d, %d: %s | %s", m_hoverX, m_hoverY, static_cast<LPCWSTR>(pixel(0)), static_cast<LPCWSTR>(pixel(1)));
            return text;
        }
        CRect panel;
        if (m_panels[0] && m_panels[0]->m_hWnd)
            m_panels[0]->GetClientRect(&panel);
        text.Format(L"Zoom %d%%%s", static_cast<int>(Scale(panel) * 100 + 0.5), m_fit ? L" (fit)" : L"");
        return text;
    }

    void ImageCompareWnd::NotifyFrame()
    {
        if (CFrameWnd* frame = GetParentFrame())
            frame->PostMessage(WM_MF_STATECHANGED);
    }

    void ImageCompareWnd::Layout()
    {
        if (!m_hWnd || !m_panels[0])
            return;
        CRect client;
        GetClientRect(&client);
        const int top = GetFonts().Scale(30);
        const int gap = GetFonts().Scale(6);
        const int width = (client.Width() - 2 * gap) / 3;
        for (int which = 0; which < 3; ++which)
        {
            const int x = which * (width + gap);
            const int w = which == 2 ? client.Width() - x : width;
            m_panels[which]->MoveWindow(x, top, w, (std::max)(0, static_cast<int>(client.Height()) - top));
        }
        Invalidate(FALSE);
    }

    void ImageCompareWnd::OnSize(UINT type, int cx, int cy)
    {
        TabWindow::OnSize(type, cx, cy);
        Layout();
    }

    BOOL ImageCompareWnd::OnEraseBkgnd(CDC*)
    {
        return TRUE;
    }

    void ImageCompareWnd::OnPaint()
    {
        CPaintDC dc(this);
        CRect client;
        GetClientRect(&client);
        const Theme& theme = GetTheme();
        Fonts& fonts = GetFonts();
        dc.FillSolidRect(&client, theme.chromeBack);
        dc.SetBkMode(TRANSPARENT);
        const int gap = fonts.Scale(6);
        const int width = (client.Width() - 2 * gap) / 3;
        for (int which = 0; which < 3; ++which)
        {
            const int x = which * (width + gap);
            CRect bar(x, fonts.Scale(3), which == 2 ? client.right : x + width, fonts.Scale(27));
            CBrush back(theme.pathBarBack);
            CPen border(PS_SOLID, 1, theme.chromeBorder);
            CBrush* ob = dc.SelectObject(&back);
            CPen* op = dc.SelectObject(&border);
            dc.RoundRect(&bar, CPoint(fonts.Scale(6), fonts.Scale(6)));
            dc.SelectObject(ob);
            dc.SelectObject(op);
            CRect text = bar;
            text.DeflateRect(fonts.Scale(8), 0);
            CFont* oldFont = dc.SelectObject(&fonts.ui);
            dc.SetTextColor(which == 2 ? theme.conflictLine : theme.text);
            CString label = which == 2 ? CString(L"Differences (highlighted in red over the left image)") : CString(m_paths[which].c_str());
            dc.DrawText(label, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
            dc.SelectObject(oldFont);
        }
    }
}
