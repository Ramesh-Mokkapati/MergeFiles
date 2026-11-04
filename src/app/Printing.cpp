// Printing.cpp - print models, pagination, page drawing, printing and the print preview.
#include "Printing.h"

#include "Theme.h"
#include "resource.h"

namespace mf
{
    namespace
    {
        // Tabs become spaces (tab stops of 4); ranges are moved to match.
        void ExpandTabs(std::wstring& text, std::vector<CharRange>& ranges)
        {
            if (text.find(L'\t') == std::wstring::npos)
                return;
            std::vector<int> map(text.size() + 1);
            std::wstring out;
            for (size_t i = 0; i < text.size(); ++i)
            {
                map[i] = static_cast<int>(out.size());
                if (text[i] == L'\t')
                    out.append(4 - out.size() % 4, L' ');
                else
                    out.push_back(text[i]);
            }
            map[text.size()] = static_cast<int>(out.size());
            for (CharRange& range : ranges)
            {
                const int end = map[static_cast<size_t>((std::min)(static_cast<int>(text.size()), range.start + range.length))];
                range.start = map[static_cast<size_t>((std::min)(static_cast<int>(text.size()), range.start))];
                range.length = end - range.start;
            }
            text = out;
        }

        std::wstring Leaf(const std::wstring& path)
        {
            const size_t slash = path.find_last_of(L"\\/");
            return slash == std::wstring::npos ? path : path.substr(slash + 1);
        }

        std::wstring Today()
        {
            SYSTEMTIME time = {};
            GetLocalTime(&time);
            wchar_t text[32];
            swprintf_s(text, L"%04d-%02d-%02d %02d:%02d", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
            return text;
        }
    }

    PrintModel BuildPrintModel(const Comparison& comparison)
    {
        const Theme& theme = GetTheme();
        PrintModel model;
        const int panes = comparison.PaneCount();
        static const wchar_t* twoWay[] = { L"Left", L"Right" };
        static const wchar_t* threeWay[] = { L"Left", L"Ancestor", L"Right" };
        for (int pane = 0; pane < panes; ++pane)
        {
            const TextBuffer& buffer = comparison.Buffer(pane);
            const std::wstring name = buffer.path.empty() ? buffer.title : buffer.path;
            model.columns.push_back({ std::wstring(panes == 3 ? threeWay[pane] : twoWay[pane]) + L": " + name, true, 1 });
            model.files += (pane ? L"  \x2194  " : L"") + buffer.title;
        }
        model.title = model.files;
        const int reference = comparison.ReferencePane();
        for (const Chunk& chunk : comparison.Chunks())
        {
            int height = 0;
            for (int pane = 0; pane < panes; ++pane)
                height = (std::max)(height, chunk.count[pane]);
            // Character detail for each pane of a difference.
            std::vector<std::vector<char>> paired(static_cast<size_t>(panes));
            std::vector<std::vector<std::vector<CharRange>>> ranges(static_cast<size_t>(panes));
            if (chunk.IsDifference())
            {
                for (int pane = 0; pane < panes; ++pane)
                {
                    int partner = 1 - pane;
                    if (comparison.IsThreeWay())
                        partner = pane == 1 ? (chunk.kind == ChunkKind::RightOnly ? 2 : 0) : 1;
                    std::vector<const std::wstring*> mine, theirs;
                    for (int line = chunk.start[pane]; line < chunk.End(pane); ++line)
                        mine.push_back(&comparison.Buffer(pane).LineText(line));
                    for (int line = chunk.start[partner]; line < chunk.End(partner); ++line)
                        theirs.push_back(&comparison.Buffer(partner).LineText(line));
                    paired[static_cast<size_t>(pane)].assign(mine.size(), 0);
                    ranges[static_cast<size_t>(pane)].resize(mine.size());
                    for (const auto& pair : PairLines(mine, theirs, comparison.Options()))
                    {
                        paired[static_cast<size_t>(pane)][static_cast<size_t>(pair.first)] = 1;
                        ranges[static_cast<size_t>(pane)][static_cast<size_t>(pair.first)] =
                            DiffLineChars(*mine[static_cast<size_t>(pair.first)], *theirs[static_cast<size_t>(pair.second)], comparison.Options()).a;
                    }
                }
            }
            for (int rel = 0; rel < height; ++rel)
            {
                PrintRow row;
                row.changed = chunk.IsDifference();
                row.blockStart = rel == 0 && chunk.IsDifference();
                ChunkStyle rowStyle = ChunkStyle::None;
                for (int pane = 0; pane < panes; ++pane)
                {
                    PrintCell cell;
                    if (rel >= chunk.count[pane])
                    {
                        cell.pad = true;
                        row.cells.push_back(cell);
                        continue;
                    }
                    const int line = chunk.start[pane] + rel;
                    const Line& data = comparison.Buffer(pane).GetLine(line);
                    ChunkStyle style = comparison.StyleInPane(chunk, pane);
                    if (chunk.IsDifference() && (style == ChunkStyle::Change || style == ChunkStyle::Conflict))
                    {
                        if (paired[static_cast<size_t>(pane)][static_cast<size_t>(rel)])
                            cell.ranges = ranges[static_cast<size_t>(pane)][static_cast<size_t>(rel)];
                        else if (style == ChunkStyle::Change)
                            style = pane == reference ? ChunkStyle::Removal : ChunkStyle::Insertion;
                    }
                    if (style != ChunkStyle::None)
                        rowStyle = style;
                    cell.number = std::to_wstring(line + 1);
                    cell.text = data.text;
                    cell.back = theme.StyleBack(style);
                    cell.rangeBack = theme.StyleChar(style);
                    ExpandTabs(cell.text, cell.ranges);
                    if ((data.flags & LineBookmark) && !data.comment.empty())
                        row.note += (row.note.empty() ? L"" : L"   ") + (L"\x270E " + std::wstring(panes == 3 ? threeWay[pane] : twoWay[pane]) + L" " + cell.number + L": " + data.comment);
                    row.cells.push_back(cell);
                }
                if (row.changed)
                    row.marker = theme.StyleLine(rowStyle == ChunkStyle::None ? ChunkStyle::Change : rowStyle);
                model.rows.push_back(std::move(row));
            }
        }
        return model;
    }

    PrintModel BuildFolderPrintModel(const FolderComparison& comparison)
    {
        const Theme& theme = GetTheme();
        PrintModel model;
        const int sides = comparison.SideCount();
        static const wchar_t* twoWay[] = { L"Left", L"Right" };
        static const wchar_t* threeWay[] = { L"Left", L"Ancestor", L"Right" };
        model.columns.push_back({ L"Name", false, 4 });
        model.columns.push_back({ L"Status", false, 2 });
        for (int side = 0; side < sides; ++side)
        {
            model.columns.push_back({ std::wstring(sides == 3 ? threeWay[side] : twoWay[side]) + L" size", false, 1 });
            model.columns.push_back({ L"Modified", false, 2 });
            model.files += (side ? L"  \x2194  " : L"") + Leaf(comparison.Root(side));
        }
        model.title = L"Folder comparison: " + model.files;
        std::vector<std::pair<int, int>> stack = { { 0, -1 } };
        while (!stack.empty())
        {
            const auto [item, depth] = stack.back();
            stack.pop_back();
            const FolderItem& entry = comparison.Item(item);
            for (auto it = entry.children.rbegin(); it != entry.children.rend(); ++it)
                stack.emplace_back(*it, depth + 1);
            if (item == 0)
                continue;
            COLORREF back = theme.paneBack;
            switch (entry.status)
            {
            case ItemStatus::LeftOnly:
            case ItemStatus::LeftChanged:
                back = theme.removeBack;
                break;
            case ItemStatus::RightOnly:
            case ItemStatus::RightChanged:
                back = theme.insertBack;
                break;
            case ItemStatus::Different:
            case ItemStatus::BothSame:
                back = theme.changeBack;
                break;
            case ItemStatus::Conflict:
            case ItemStatus::Error:
                back = theme.conflictBack;
                break;
            default:
                break;
            }
            PrintRow row;
            row.changed = entry.status != ItemStatus::Identical;
            auto cell = [&](const std::wstring& text) {
                PrintCell c;
                c.text = text;
                c.back = back;
                row.cells.push_back(c);
            };
            cell(std::wstring(static_cast<size_t>(depth) * 2, L' ') + entry.name + (entry.isDir ? L"\\" : L""));
            cell(StatusName(entry.status));
            for (int side = 0; side < sides; ++side)
            {
                const FolderSide& info = entry.side[side];
                wchar_t time[32] = {};
                if (info.exists && info.modified > 0)
                {
                    FILETIME utc = { static_cast<DWORD>(info.modified & 0xFFFFFFFF), static_cast<DWORD>(static_cast<uint64_t>(info.modified) >> 32) };
                    FILETIME local = {};
                    SYSTEMTIME st = {};
                    if (FileTimeToLocalFileTime(&utc, &local) && FileTimeToSystemTime(&local, &st))
                        swprintf_s(time, L"%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
                }
                cell(!info.exists ? L"\x2014" : (info.isDir ? L"" : std::to_wstring(info.size)));
                cell(time);
            }
            model.rows.push_back(std::move(row));
        }
        return model;
    }

    // ---- layout ----

    void PrintLayout::MakeFonts(int dpi)
    {
        if (dpi == m_fontDpi)
            return;
        m_fontDpi = dpi;
        auto make = [&](CFont& font, const wchar_t* face, int points, int weight) {
            font.DeleteObject();
            LOGFONTW lf = {};
            lf.lfHeight = -MulDiv(points, dpi, 72);
            lf.lfWeight = weight;
            lf.lfCharSet = DEFAULT_CHARSET;
            lf.lfQuality = CLEARTYPE_QUALITY;
            wcscpy_s(lf.lfFaceName, face);
            font.CreateFontIndirectW(&lf);
        };
        make(m_font, L"Consolas", m_settings.fontPoints, FW_NORMAL);
        make(m_bold, L"Segoe UI", m_settings.fontPoints + 1, FW_SEMIBOLD);
        make(m_small, L"Segoe UI", (std::max)(6, m_settings.fontPoints - 1), FW_NORMAL);
    }

    void PrintLayout::Measure(CDC& dc, const CRect& page, int dpi)
    {
        MakeFonts(dpi);
        Metrics& m = m_metrics;
        m.dpi = dpi;
        CFont* old = dc.SelectObject(&m_font);
        TEXTMETRICW tm = {};
        dc.GetTextMetrics(&tm);
        m.charWidth = (std::max)(1, static_cast<int>(dc.GetTextExtent(L"0", 1).cx));
        m.lineHeight = tm.tmHeight + MulDiv(1, dpi, 96);
        dc.SelectObject(&m_bold);
        dc.GetTextMetrics(&tm);
        m.headerHeight = tm.tmHeight * 2;
        m.columnHeader = tm.tmHeight + MulDiv(6, dpi, 96);
        dc.SelectObject(old);
        m.margin = dpi / 2;  // half an inch
        m.body = CRect(page.left + m.margin, page.top + m.margin + m.headerHeight + m.columnHeader, page.right - m.margin, page.bottom - m.margin - m.headerHeight);

        // Columns: share the width by weight, after the number areas and the gutters between them.
        const int columns = static_cast<int>(m_model.columns.size());
        const int gutter = m.charWidth * 2;
        const int numberWidth = m.charWidth * 6;
        int fixed = gutter * (columns - 1);
        int weights = 0;
        for (const PrintColumn& column : m_model.columns)
        {
            fixed += column.numbered ? numberWidth : 0;
            weights += column.weight;
        }
        const int free = (std::max)(m.charWidth * 10, m.body.Width() - fixed);
        m.columnLeft.clear();
        m.textLeft.clear();
        m.textChars.clear();
        int x = m.body.left;
        for (const PrintColumn& column : m_model.columns)
        {
            m.columnLeft.push_back(x);
            if (column.numbered)
                x += numberWidth;
            m.textLeft.push_back(x);
            const int width = free * column.weight / (std::max)(1, weights);
            m.textChars.push_back((std::max)(4, width / m.charWidth - 1));
            x += width + gutter;
        }
    }

    std::wstring PrintLayout::Expand(const std::wstring& text) const
    {
        std::wstring out = text;
        for (wchar_t& c : out)
        {
            if (c < 0x20)
                c = L' ';
        }
        return out;
    }

    int PrintLayout::RowHeight(const PrintRow& row) const
    {
        int lines = 1;
        for (size_t c = 0; c < row.cells.size() && c < m_metrics.textChars.size(); ++c)
        {
            const int chars = m_metrics.textChars[c];
            const int length = static_cast<int>(row.cells[c].text.size());
            lines = (std::max)(lines, (length + chars - 1) / chars);
        }
        return lines * m_metrics.lineHeight + (row.note.empty() ? 0 : m_metrics.lineHeight);
    }

    void PrintLayout::Paginate(CDC& dc, const CRect& page, int dpi)
    {
        Measure(dc, page, dpi);
        m_pages.clear();
        std::vector<std::pair<int, int>> all;
        int first = 0, y = 0;
        const int height = m_metrics.body.Height();
        for (int row = 0; row < static_cast<int>(m_model.rows.size()); ++row)
        {
            const int rowHeight = (std::min)(height, RowHeight(m_model.rows[static_cast<size_t>(row)]));
            if (y + rowHeight > height && row > first)
            {
                all.emplace_back(first, row);
                first = row;
                y = 0;
            }
            y += rowHeight;
        }
        all.emplace_back(first, static_cast<int>(m_model.rows.size()));
        for (const auto& range : all)
        {
            bool changed = !m_settings.changedPagesOnly;
            for (int row = range.first; row < range.second && !changed; ++row)
                changed = m_model.rows[static_cast<size_t>(row)].changed;
            if (changed)
                m_pages.push_back(range);
        }
        m_totalPages = static_cast<int>(m_pages.size());
    }

    std::wstring PrintLayout::Token(const std::wstring& text, int page) const
    {
        std::wstring out = text;
        auto replace = [&](const wchar_t* key, const std::wstring& value) {
            for (size_t at = out.find(key); at != std::wstring::npos; at = out.find(key, at + value.size()))
                out.replace(at, wcslen(key), value);
        };
        replace(L"{title}", m_model.title);
        replace(L"{files}", m_model.files);
        replace(L"{date}", Today());
        replace(L"{page}", std::to_wstring(page + 1));
        replace(L"{pages}", std::to_wstring(m_totalPages));
        return out;
    }

    void PrintLayout::DrawPage(CDC& dc, int index, const CRect& page, int dpi)
    {
        if (m_metrics.dpi != dpi || m_metrics.body.IsRectEmpty())
            Measure(dc, page, dpi);
        if (index < 0 || index >= static_cast<int>(m_pages.size()))
            return;
        const Theme& theme = GetTheme();
        const Metrics& m = m_metrics;
        dc.SetBkMode(TRANSPARENT);
        CFont* old = dc.SelectObject(&m_bold);

        // Header and footer.
        dc.SetTextColor(RGB(30, 30, 30));
        CRect header(page.left + m.margin, page.top + m.margin, page.right - m.margin, page.top + m.margin + m.headerHeight / 2);
        dc.DrawText(Token(m_settings.header, index).c_str(), -1, &header, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        dc.FillSolidRect(header.left, header.bottom + m.headerHeight / 4, header.Width(), (std::max)(1, dpi / 96), RGB(160, 160, 160));
        dc.SelectObject(&m_small);
        dc.SetTextColor(RGB(90, 90, 90));
        CRect footer(page.left + m.margin, page.bottom - m.margin - m.headerHeight / 2, page.right - m.margin, page.bottom - m.margin);
        dc.DrawText(Token(m_settings.footer, index).c_str(), -1, &footer, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        // Column headers.
        dc.SelectObject(&m_bold);
        const int columnTop = m.body.top - m.columnHeader;
        dc.FillSolidRect(m.body.left, columnTop, m.body.Width(), m.columnHeader - dpi / 48, theme.pathBarBack);
        dc.SetTextColor(theme.chromeText);
        for (size_t c = 0; c < m_model.columns.size(); ++c)
        {
            const int right = c + 1 < m_model.columns.size() ? m.columnLeft[c + 1] - m.charWidth : m.body.right;
            CRect r(m.columnLeft[c] + m.charWidth / 2, columnTop, right, m.body.top - dpi / 48);
            dc.DrawText(m_model.columns[c].title.c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
        }

        // Rows.
        dc.SelectObject(&m_font);
        int y = m.body.top;
        const auto [firstRow, endRow] = m_pages[static_cast<size_t>(index)];
        CBrush hatch(HS_BDIAGONAL, RGB(205, 205, 210));
        for (int r = firstRow; r < endRow; ++r)
        {
            const PrintRow& row = m_model.rows[static_cast<size_t>(r)];
            const int height = (std::min)(m.body.Height(), RowHeight(row));
            const int textHeight = height - (row.note.empty() ? 0 : m.lineHeight);
            for (size_t c = 0; c < row.cells.size() && c < m_model.columns.size(); ++c)
            {
                const PrintCell& cell = row.cells[c];
                const int right = c + 1 < m_model.columns.size() ? m.columnLeft[c + 1] - m.charWidth * 2 : m.body.right;
                CRect area(m.columnLeft[c], y, right, y + textHeight);
                if (cell.pad)
                {
                    dc.FillSolidRect(&area, RGB(240, 240, 242));
                    dc.SetBkColor(RGB(240, 240, 242));
                    dc.FillRect(&area, &hatch);
                    continue;
                }
                dc.FillSolidRect(&area, cell.back);
                if (!cell.number.empty())
                {
                    dc.SetTextColor(RGB(140, 140, 140));
                    CRect number(m.columnLeft[c], y, m.textLeft[c] - m.charWidth, y + m.lineHeight);
                    dc.DrawText(cell.number.c_str(), -1, &number, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
                }
                const std::wstring text = Expand(cell.text);
                const int chars = m.textChars[c];
                for (int segment = 0; segment * chars < static_cast<int>(text.size()) || segment == 0; ++segment)
                {
                    const int start = segment * chars;
                    const int length = (std::min)(chars, static_cast<int>(text.size()) - start);
                    const int lineY = y + segment * m.lineHeight;
                    if (lineY + m.lineHeight > y + textHeight)
                        break;
                    for (const CharRange& range : cell.ranges)
                    {
                        const int from = (std::max)(start, range.start);
                        const int to = (std::min)(start + chars, range.start + range.length);
                        if (to > from)
                            dc.FillSolidRect(m.textLeft[c] + (from - start) * m.charWidth, lineY, (to - from) * m.charWidth, m.lineHeight, cell.rangeBack);
                    }
                    if (length > 0)
                    {
                        dc.SetTextColor(RGB(20, 20, 20));
                        std::vector<int> dx(static_cast<size_t>(length), m.charWidth);
                        ::ExtTextOutW(dc.m_hDC, m.textLeft[c], lineY, 0, nullptr, text.c_str() + start, static_cast<UINT>(length), dx.data());
                    }
                }
            }
            // The link between the columns of a changed row.
            if (row.marker != CLR_INVALID)
            {
                for (size_t c = 1; c < m_model.columns.size(); ++c)
                    dc.FillSolidRect(m.columnLeft[c] - m.charWidth * 2 + m.charWidth / 2, y, m.charWidth, textHeight, row.marker);
            }
            if (row.blockStart)
                dc.FillSolidRect(m.body.left, y, m.body.Width(), (std::max)(1, dpi / 150), row.marker == CLR_INVALID ? RGB(160, 160, 160) : row.marker);
            if (!row.note.empty())
            {
                CRect note(m.body.left, y + textHeight, m.body.right, y + height);
                dc.FillSolidRect(&note, RGB(255, 248, 214));
                dc.SelectObject(&m_small);
                dc.SetTextColor(RGB(90, 74, 0));
                note.left += m.charWidth;
                dc.DrawText(row.note.c_str(), -1, &note, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                dc.SelectObject(&m_font);
            }
            y += height;
        }
        dc.SelectObject(old);
    }

    // ---- printing ----

    namespace
    {
        // A DEVMODE with the wanted orientation, for the print dialog to start from.
        HGLOBAL DevModeFor(bool landscape)
        {
            PRINTDLG defaults = { sizeof(defaults) };
            if (!AfxGetApp()->GetPrinterDeviceDefaults(&defaults) || !defaults.hDevMode)
                return nullptr;
            const SIZE_T size = GlobalSize(defaults.hDevMode);
            HGLOBAL copy = GlobalAlloc(GHND, size);
            if (!copy)
                return nullptr;
            void* from = GlobalLock(defaults.hDevMode);
            DEVMODE* to = static_cast<DEVMODE*>(GlobalLock(copy));
            memcpy(to, from, size);
            to->dmFields |= DM_ORIENTATION;
            to->dmOrientation = static_cast<short>(landscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT);
            GlobalUnlock(copy);
            GlobalUnlock(defaults.hDevMode);
            return copy;
        }

        CRect PrintableArea(CDC& dc)
        {
            return CRect(0, 0, dc.GetDeviceCaps(HORZRES), dc.GetDeviceCaps(VERTRES));
        }
    }

    bool PrintModelToPrinter(const PrintModel& model, const PageSettings& settings, CWnd* parent)
    {
        CPrintDialog dialog(FALSE, PD_ALLPAGES | PD_USEDEVMODECOPIES | PD_NOSELECTION | PD_HIDEPRINTTOFILE, parent);
        dialog.m_pd.hDevMode = DevModeFor(settings.landscape);
        if (dialog.DoModal() != IDOK)
            return false;
        HDC printer = dialog.GetPrinterDC();
        if (!printer)
        {
            AfxMessageBox(L"No printer is available.", MB_ICONEXCLAMATION);
            return false;
        }
        CDC dc;
        dc.Attach(printer);
        const CRect page = PrintableArea(dc);
        const int dpi = dc.GetDeviceCaps(LOGPIXELSY);
        PrintLayout layout(model, settings);
        layout.Paginate(dc, page, dpi);
        DOCINFO info = { sizeof(info) };
        const std::wstring name = L"MergeFiles - " + model.title;
        info.lpszDocName = name.c_str();
        bool ok = dc.StartDoc(&info) > 0;
        for (int index = 0; ok && index < layout.PageCount(); ++index)
        {
            ok = dc.StartPage() > 0;
            if (ok)
            {
                layout.DrawPage(dc, index, page, dpi);
                ok = dc.EndPage() > 0;
            }
        }
        if (ok)
            dc.EndDoc();
        else
            dc.AbortDoc();
        dc.DeleteDC();
        if (!ok)
            AfxMessageBox(L"Printing failed.", MB_ICONEXCLAMATION);
        return ok;
    }

    // ---- print preview ----

    namespace
    {
        // The paper, in inches, of the default printer (A4 if there is none), in the wanted orientation.
        void PaperSize(bool landscape, double& width, double& height)
        {
            width = 8.27;
            height = 11.69;
            if (HGLOBAL devMode = DevModeFor(landscape))
            {
                PRINTDLG defaults = { sizeof(defaults) };
                if (AfxGetApp()->GetPrinterDeviceDefaults(&defaults) && defaults.hDevNames)
                {
                    DEVNAMES* names = static_cast<DEVNAMES*>(GlobalLock(defaults.hDevNames));
                    DEVMODE* mode = static_cast<DEVMODE*>(GlobalLock(devMode));
                    HDC dc = CreateDCW(reinterpret_cast<const wchar_t*>(names) + names->wDriverOffset, reinterpret_cast<const wchar_t*>(names) + names->wDeviceOffset, nullptr, mode);
                    GlobalUnlock(devMode);
                    GlobalUnlock(defaults.hDevNames);
                    if (dc)
                    {
                        width = static_cast<double>(GetDeviceCaps(dc, PHYSICALWIDTH)) / GetDeviceCaps(dc, LOGPIXELSX);
                        height = static_cast<double>(GetDeviceCaps(dc, PHYSICALHEIGHT)) / GetDeviceCaps(dc, LOGPIXELSY);
                        DeleteDC(dc);
                        GlobalFree(devMode);
                        return;
                    }
                }
                GlobalFree(devMode);
            }
            if (landscape)
                std::swap(width, height);
        }

        class PreviewDlg : public CDialog
        {
        public:
            PreviewDlg(const PrintModel& model, const PageSettings& settings, CWnd* parent)
                : CDialog(IDD_PRINT_PREVIEW, parent), m_model(model), m_settings(settings), m_layout(model, settings)
            {
                PaperSize(settings.landscape, m_paperWidth, m_paperHeight);
            }

        protected:
            BOOL OnInitDialog() override
            {
                CDialog::OnInitDialog();
                SetWindowTextW((L"Print Preview - " + m_model.title).c_str());
                CRect work;
                SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
                work.DeflateRect(work.Width() / 10, work.Height() / 14);
                MoveWindow(&work);
                Layout();
                return TRUE;
            }

            afx_msg void OnSize(UINT type, int cx, int cy)
            {
                CDialog::OnSize(type, cx, cy);
                Layout();
            }

            afx_msg BOOL OnEraseBkgnd(CDC*)
            {
                return TRUE;
            }

            // The page label sits on the button bar.
            afx_msg HBRUSH OnCtlColor(CDC* dc, CWnd* wnd, UINT type)
            {
                if (wnd && wnd->GetDlgCtrlID() == IDC_PV_PAGE)
                {
                    if (!m_barBrush.m_hObject)
                        m_barBrush.CreateSolidBrush(GetTheme().chromeBack);
                    dc->SetBkColor(GetTheme().chromeBack);
                    dc->SetTextColor(GetTheme().chromeText);
                    return m_barBrush;
                }
                return CDialog::OnCtlColor(dc, wnd, type);
            }

            afx_msg void OnPaint()
            {
                CPaintDC paint(this);
                CRect client;
                GetClientRect(&client);
                const int bar = GetFonts().Scale(40);
                CDC dc;
                dc.CreateCompatibleDC(&paint);
                CBitmap bitmap;
                bitmap.CreateCompatibleBitmap(&paint, client.Width(), client.Height());
                CBitmap* oldBitmap = dc.SelectObject(&bitmap);
                dc.FillSolidRect(&client, RGB(128, 128, 134));
                dc.FillSolidRect(0, 0, client.Width(), bar, GetTheme().chromeBack);
                const CRect page = PageRect();
                dc.FillSolidRect(page.left + 4, page.top + 4, page.Width(), page.Height(), RGB(70, 70, 76));
                dc.FillSolidRect(&page, RGB(255, 255, 255));
                // Draw at the preview's own resolution: the page is paperWidth inches wide.
                const int dpi = (std::max)(20, static_cast<int>(page.Width() / m_paperWidth));
                CRect origin(0, 0, page.Width(), page.Height());
                dc.SetViewportOrg(page.left, page.top);
                if (m_paginatedDpi != dpi)
                {
                    m_layout.Paginate(dc, origin, dpi);
                    m_paginatedDpi = dpi;
                    m_page = (std::min)(m_page, (std::max)(0, m_layout.PageCount() - 1));
                }
                CRgn clip;
                clip.CreateRectRgnIndirect(&page);
                dc.SelectClipRgn(&clip);
                m_layout.DrawPage(dc, m_page, origin, dpi);
                dc.SelectClipRgn(nullptr);
                dc.SetViewportOrg(0, 0);
                paint.BitBlt(0, 0, client.Width(), client.Height(), &dc, 0, 0, SRCCOPY);
                dc.SelectObject(oldBitmap);
                UpdateControls();
            }

            BOOL OnCommand(WPARAM wParam, LPARAM lParam) override
            {
                switch (LOWORD(wParam))
                {
                case IDC_PV_PREV:
                    Go(m_page - 1);
                    return TRUE;
                case IDC_PV_NEXT:
                    Go(m_page + 1);
                    return TRUE;
                case IDC_PV_PRINT:
                    if (PrintModelToPrinter(m_model, m_settings, this))
                        EndDialog(IDOK);
                    return TRUE;
                default:
                    return CDialog::OnCommand(wParam, lParam);
                }
            }

            BOOL PreTranslateMessage(MSG* msg) override
            {
                if (msg->message == WM_KEYDOWN)
                {
                    if (msg->wParam == VK_NEXT || msg->wParam == VK_RIGHT)
                    {
                        Go(m_page + 1);
                        return TRUE;
                    }
                    if (msg->wParam == VK_PRIOR || msg->wParam == VK_LEFT)
                    {
                        Go(m_page - 1);
                        return TRUE;
                    }
                }
                if (msg->message == WM_MOUSEWHEEL)
                {
                    Go(m_page + (GET_WHEEL_DELTA_WPARAM(msg->wParam) > 0 ? -1 : 1));
                    return TRUE;
                }
                return CDialog::PreTranslateMessage(msg);
            }

            DECLARE_MESSAGE_MAP()

        private:
            CRect PageRect() const
            {
                CRect client;
                GetClientRect(&client);
                const int bar = GetFonts().Scale(40);
                CRect area(client.left + 20, client.top + bar + 16, client.right - 20, client.bottom - 20);
                double width = area.Width(), height = width * m_paperHeight / m_paperWidth;
                if (height > area.Height())
                {
                    height = area.Height();
                    width = height * m_paperWidth / m_paperHeight;
                }
                const int left = area.left + static_cast<int>((area.Width() - width) / 2);
                return CRect(left, area.top, left + static_cast<int>(width), area.top + static_cast<int>(height));
            }

            void Layout()
            {
                if (!GetDlgItem(IDC_PV_PREV))
                    return;
                const int bar = GetFonts().Scale(40);
                const int h = GetFonts().Scale(26), y = (bar - h) / 2;
                int x = GetFonts().Scale(10);
                for (UINT id : { IDC_PV_PRINT, IDC_PV_PREV, IDC_PV_NEXT, IDCANCEL })
                {
                    GetDlgItem(id)->MoveWindow(x, y, GetFonts().Scale(90), h);
                    x += GetFonts().Scale(96);
                }
                GetDlgItem(IDC_PV_PAGE)->MoveWindow(x + GetFonts().Scale(10), y, GetFonts().Scale(300), h);
                Invalidate(FALSE);
            }

            void Go(int page)
            {
                if (page < 0 || page >= m_layout.PageCount())
                    return;
                m_page = page;
                Invalidate(FALSE);
            }

            void UpdateControls()
            {
                CString text;
                if (m_layout.PageCount() == 0)
                    text = L"Nothing to print (no page has changes).";
                else
                    text.Format(L"Page %d of %d", m_page + 1, m_layout.PageCount());
                CString current;
                GetDlgItemTextW(IDC_PV_PAGE, current);
                if (current != text)
                    SetDlgItemTextW(IDC_PV_PAGE, text);
                GetDlgItem(IDC_PV_PREV)->EnableWindow(m_page > 0);
                GetDlgItem(IDC_PV_NEXT)->EnableWindow(m_page + 1 < m_layout.PageCount());
            }

            const PrintModel& m_model;
            PageSettings m_settings;
            PrintLayout m_layout;
            double m_paperWidth = 11.69, m_paperHeight = 8.27;
            int m_paginatedDpi = 0;
            int m_page = 0;
            CBrush m_barBrush;
        };

        BEGIN_MESSAGE_MAP(PreviewDlg, CDialog)
            ON_WM_SIZE()
            ON_WM_ERASEBKGND()
            ON_WM_CTLCOLOR()
            ON_WM_PAINT()
        END_MESSAGE_MAP()
    }

    void ShowPrintPreview(const PrintModel& model, const PageSettings& settings, CWnd* parent)
    {
        PreviewDlg dialog(model, settings, parent);
        dialog.DoModal();
    }
}
