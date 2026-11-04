// Theme.cpp - colours and fonts.
#include "Theme.h"

namespace mf
{
    COLORREF Theme::StyleBack(ChunkStyle style) const
    {
        switch (style)
        {
        case ChunkStyle::Insertion:
            return insertBack;
        case ChunkStyle::Removal:
            return removeBack;
        case ChunkStyle::Change:
            return changeBack;
        case ChunkStyle::Conflict:
            return conflictBack;
        case ChunkStyle::Ignored:
            return ignoredBack;
        default:
            return paneBack;
        }
    }

    COLORREF Theme::StyleChar(ChunkStyle style) const
    {
        switch (style)
        {
        case ChunkStyle::Insertion:
            return insertChar;
        case ChunkStyle::Removal:
            return removeChar;
        case ChunkStyle::Conflict:
            return conflictChar;
        case ChunkStyle::Ignored:
            return ignoredBack;
        default:
            return changeChar;
        }
    }

    COLORREF Theme::StyleLine(ChunkStyle style) const
    {
        switch (style)
        {
        case ChunkStyle::Insertion:
            return insertLine;
        case ChunkStyle::Removal:
            return removeLine;
        case ChunkStyle::Conflict:
            return conflictLine;
        case ChunkStyle::Ignored:
            return ignoredLine;
        default:
            return changeLine;
        }
    }

    COLORREF Theme::TokenColor(TokenClass cls) const
    {
        switch (cls)
        {
        case TokenClass::Keyword:
            return keyword;
        case TokenClass::Type:
            return type;
        case TokenClass::Comment:
            return comment;
        case TokenClass::String:
            return string;
        case TokenClass::Number:
            return number;
        case TokenClass::Preprocessor:
            return preprocessor;
        case TokenClass::Tag:
            return tag;
        case TokenClass::Attribute:
            return attribute;
        default:
            return text;
        }
    }

    Theme& EditableTheme()
    {
        static Theme theme;
        return theme;
    }

    const Theme& GetTheme()
    {
        return EditableTheme();
    }

    const std::vector<ThemeColor>& ThemeColors()
    {
        static const std::vector<ThemeColor> colors = {
            { L"Text background", L"PaneBack", &Theme::paneBack },
            { L"Text", L"Text", &Theme::text },
            { L"Insertion", L"InsertBack", &Theme::insertBack },
            { L"Insertion: changed characters", L"InsertChar", &Theme::insertChar },
            { L"Removal", L"RemoveBack", &Theme::removeBack },
            { L"Removal: changed characters", L"RemoveChar", &Theme::removeChar },
            { L"Change", L"ChangeBack", &Theme::changeBack },
            { L"Change: changed characters", L"ChangeChar", &Theme::changeChar },
            { L"Conflict", L"ConflictBack", &Theme::conflictBack },
            { L"Conflict: changed characters", L"ConflictChar", &Theme::conflictChar },
            { L"Ignored difference", L"IgnoredBack", &Theme::ignoredBack },
            { L"Selection", L"Selection", &Theme::selection },
            { L"Keyword", L"Keyword", &Theme::keyword },
            { L"Comment", L"Comment", &Theme::comment },
            { L"String", L"String", &Theme::string },
            { L"Bookmark", L"Bookmark", &Theme::bookmark },
        };
        return colors;
    }

    namespace
    {
        int CALLBACK FontExists(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found)
        {
            *reinterpret_cast<bool*>(found) = true;
            return 0;
        }

        bool HasFont(const wchar_t* face)
        {
            bool found = false;
            HDC dc = ::GetDC(nullptr);
            LOGFONTW lf = {};
            lf.lfCharSet = DEFAULT_CHARSET;
            wcscpy_s(lf.lfFaceName, face);
            EnumFontFamiliesExW(dc, &lf, FontExists, reinterpret_cast<LPARAM>(&found), 0);
            ::ReleaseDC(nullptr, dc);
            return found;
        }

        void MakeFont(CFont& font, const wchar_t* face, int pixelHeight, int weight)
        {
            font.DeleteObject();
            LOGFONTW lf = {};
            lf.lfHeight = -pixelHeight;
            lf.lfWeight = weight;
            lf.lfCharSet = DEFAULT_CHARSET;
            lf.lfQuality = CLEARTYPE_QUALITY;
            wcscpy_s(lf.lfFaceName, face);
            font.CreateFontIndirectW(&lf);
        }
    }

    void Fonts::SetCodeFont(const std::wstring& face, int pointSize)
    {
        m_codeFace = face;
        m_codePoints = (std::max)(6, (std::min)(36, pointSize));
        m_dpi = 0;  // recreate at the next Create
    }

    void Fonts::Create(UINT dpi)
    {
        if (dpi == m_dpi)
            return;
        m_dpi = dpi;
        static const wchar_t* defaultCodeFace = HasFont(L"Cascadia Mono") ? L"Cascadia Mono" : L"Consolas";
        static const wchar_t* iconFace = HasFont(L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
        const wchar_t* codeFace = m_codeFace.empty() || !HasFont(m_codeFace.c_str()) ? defaultCodeFace : m_codeFace.c_str();
        const int codeHeight = MulDiv(m_codePoints, static_cast<int>(dpi), 72);
        MakeFont(code, codeFace, codeHeight, FW_NORMAL);
        MakeFont(codeBold, codeFace, codeHeight, FW_BOLD);
        MakeFont(ui, L"Segoe UI", Scale(13), FW_NORMAL);
        MakeFont(uiBold, L"Segoe UI", Scale(13), FW_SEMIBOLD);
        MakeFont(uiSmall, L"Segoe UI", Scale(11), FW_NORMAL);
        MakeFont(icons, iconFace, Scale(16), FW_NORMAL);
        MakeFont(iconsSmall, iconFace, Scale(10), FW_NORMAL);
    }

    Fonts& GetFonts()
    {
        static Fonts fonts;
        return fonts;
    }

    UINT WindowDpi(HWND hwnd)
    {
        const UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 0;
        return dpi ? dpi : 96;
    }

    void DrawGlyph(CDC& dc, const CRect& rect, wchar_t glyph, COLORREF color, CFont& font)
    {
        CFont* old = dc.SelectObject(&font);
        dc.SetTextColor(color);
        dc.SetBkMode(TRANSPARENT);
        wchar_t text[2] = { glyph, 0 };
        CRect r = rect;
        dc.DrawText(text, 1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        dc.SelectObject(old);
    }

    int CharColumns(wchar_t c)
    {
        if (c >= 0xDC00 && c <= 0xDFFF)
            return 0;
        if (c >= 0xD800 && c <= 0xDBFF)
            return 2;
        if ((c >= 0x0300 && c <= 0x036F) || c == 0x200B || c == 0x200C || c == 0x200D || c == 0xFEFF)
            return 0;
        if ((c >= 0x1100 && c <= 0x115F) || (c >= 0x2E80 && c <= 0xA4CF && c != 0x303F) || (c >= 0xAC00 && c <= 0xD7A3) ||
            (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFE30 && c <= 0xFE4F) || (c >= 0xFF00 && c <= 0xFF60) || (c >= 0xFFE0 && c <= 0xFFE6))
            return 2;
        return 1;
    }

    void BuildColumns(const std::wstring& text, int tabSize, std::vector<int>& cols)
    {
        const size_t n = text.size();
        cols.resize(n + 1);
        int col = 0;
        for (size_t i = 0; i < n; ++i)
        {
            cols[i] = col;
            const wchar_t c = text[i];
            col += c == L'\t' ? tabSize - col % tabSize : CharColumns(c);
        }
        cols[n] = col;
    }

    void FillSolid(CDC& dc, const CRect& rect, COLORREF color)
    {
        dc.FillSolidRect(&rect, color);
    }

    COLORREF Blend(COLORREF a, COLORREF b, int percentB)
    {
        const int pa = 100 - percentB;
        return RGB((GetRValue(a) * pa + GetRValue(b) * percentB) / 100, (GetGValue(a) * pa + GetGValue(b) * percentB) / 100,
            (GetBValue(a) * pa + GetBValue(b) * percentB) / 100);
    }
}
