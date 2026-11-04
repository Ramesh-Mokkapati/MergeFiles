// Theme.h - colours, fonts and DPI scaling shared by the custom-drawn windows.
#pragma once

#include "framework.h"

#include "Comparison.h"
#include "Syntax.h"

namespace mf
{
    struct Theme
    {
        // Text panes
        COLORREF paneBack = RGB(255, 255, 255);
        COLORREF text = RGB(30, 30, 30);
        COLORREF textResolved = RGB(160, 160, 160);
        COLORREF lineNumber = RGB(140, 140, 140);
        COLORREF lineNumberBack = RGB(255, 255, 255);
        COLORREF selection = RGB(173, 214, 255);
        COLORREF selectionInactive = RGB(220, 226, 234);
        COLORREF caret = RGB(0, 0, 0);
        COLORREF editMarker = RGB(64, 128, 230);
        COLORREF whitespace = RGB(190, 190, 190);
        COLORREF currentChunkBorder = RGB(90, 90, 90);

        // Differences: the block tint, the stronger character-level tint and the border line
        COLORREF insertBack = RGB(222, 244, 218);
        COLORREF insertChar = RGB(166, 226, 156);
        COLORREF insertLine = RGB(120, 190, 110);
        COLORREF removeBack = RGB(253, 228, 206);
        COLORREF removeChar = RGB(246, 190, 140);
        COLORREF removeLine = RGB(226, 150, 90);
        COLORREF changeBack = RGB(238, 234, 252);
        COLORREF changeChar = RGB(208, 196, 246);
        COLORREF changeLine = RGB(160, 146, 220);
        COLORREF conflictBack = RGB(252, 222, 222);
        COLORREF conflictChar = RGB(244, 170, 170);
        COLORREF conflictLine = RGB(214, 90, 90);
        COLORREF ignoredBack = RGB(243, 243, 243);
        COLORREF ignoredLine = RGB(205, 205, 205);
        COLORREF padding = RGB(236, 236, 238);
        COLORREF bookmark = RGB(43, 108, 216);
        COLORREF syncLink = RGB(30, 120, 220);
        COLORREF dropCaret = RGB(220, 60, 20);

        // Chrome
        COLORREF chromeBack = RGB(236, 236, 238);
        COLORREF chromeBorder = RGB(208, 208, 212);
        COLORREF chromeText = RGB(60, 60, 64);
        COLORREF chromeLabel = RGB(110, 110, 116);
        COLORREF buttonHover = RGB(220, 220, 226);
        COLORREF buttonPressed = RGB(200, 200, 208);
        COLORREF buttonChecked = RGB(210, 222, 240);
        COLORREF disabled = RGB(176, 176, 180);
        COLORREF tabActive = RGB(255, 255, 255);
        COLORREF tabInactive = RGB(228, 228, 232);
        COLORREF accent = RGB(43, 108, 216);
        COLORREF gutterBack = RGB(244, 244, 246);
        COLORREF linkLine = RGB(150, 150, 156);
        COLORREF pathBarBack = RGB(246, 246, 248);

        // Syntax
        COLORREF keyword = RGB(160, 30, 160);
        COLORREF type = RGB(30, 90, 180);
        COLORREF comment = RGB(0, 128, 0);
        COLORREF string = RGB(180, 60, 20);
        COLORREF number = RGB(20, 120, 140);
        COLORREF preprocessor = RGB(120, 90, 30);
        COLORREF tag = RGB(30, 90, 180);
        COLORREF attribute = RGB(170, 40, 40);

        COLORREF StyleBack(ChunkStyle style) const;
        COLORREF StyleChar(ChunkStyle style) const;
        COLORREF StyleLine(ChunkStyle style) const;
        COLORREF TokenColor(TokenClass cls) const;
    };

    const Theme& GetTheme();
    // The theme the colour options change (GetTheme returns the same object).
    Theme& EditableTheme();

    // The user-customizable colours, for the options dialog and the registry.
    struct ThemeColor
    {
        const wchar_t* name;      // shown in the options dialog
        const wchar_t* key;       // registry value name
        COLORREF Theme::*member;
    };
    const std::vector<ThemeColor>& ThemeColors();

    // Fonts for one DPI; recreated when the window moves to a monitor with another DPI.
    class Fonts
    {
    public:
        void Create(UINT dpi);
        // The compared text's font (fixed pitch); takes effect at the next Create.
        void SetCodeFont(const std::wstring& face, int pointSize);
        const std::wstring& CodeFace() const
        {
            return m_codeFace;
        }
        int CodePointSize() const
        {
            return m_codePoints;
        }
        UINT Dpi() const
        {
            return m_dpi;
        }
        int Scale(int value) const
        {
            return MulDiv(value, static_cast<int>(m_dpi), 96);
        }
        CFont code;       // the compared text (fixed pitch)
        CFont codeBold;
        CFont ui;         // labels, tabs, path bars
        CFont uiBold;
        CFont uiSmall;    // toolbar group labels
        CFont icons;      // Segoe Fluent Icons / Segoe MDL2 Assets glyphs
        CFont iconsSmall;

    private:
        UINT m_dpi = 0;
        std::wstring m_codeFace;  // empty: Cascadia Mono, else Consolas
        int m_codePoints = 10;
    };

    Fonts& GetFonts();
    UINT WindowDpi(HWND hwnd);

    // Display columns of one UTF-16 code unit: wide East Asian characters take two, combining marks
    // and the second half of a surrogate pair none.
    int CharColumns(wchar_t c);
    // cols[i] = display column where character i starts; cols[n] = the line's width in columns.
    void BuildColumns(const std::wstring& text, int tabSize, std::vector<int>& cols);

    // Draws a Segoe icon glyph centred in rect.
    void DrawGlyph(CDC& dc, const CRect& rect, wchar_t glyph, COLORREF color, CFont& font);
    void FillSolid(CDC& dc, const CRect& rect, COLORREF color);
    COLORREF Blend(COLORREF a, COLORREF b, int percentB);
}
