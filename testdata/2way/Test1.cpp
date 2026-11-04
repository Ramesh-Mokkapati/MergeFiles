// TextSearch.cpp - finding text in a document, line by line.
#include "TextSearch.h"

#include <string>

namespace search
{
    static bool matches(const wchar_t* text, int length, int column, const std::wstring& what, bool matchCase)
    {
        if (column + static_cast<int>(what.size()) > length)
            return false;
        for (size_t k = 0; k < what.size(); ++k)
        {
            wchar_t a = text[column + k];
            wchar_t b = what[k];
            if (!matchCase)
            {
                a = towlower(a);
                b = towlower(b);
            }
            if (a != b)
                return false;
        }
        return true;
    }

    template <class Action>
    bool findForward(Document* pDocument, int side, const std::wstring& strText, bool bMatchCase, const Range& range, Action& action)
    {
        bool bFound(false), bStop(false);
        const size_t nSearchLen(strText.length());

        for (LineIterator current(range.begin(), pDocument, side); !bStop && current < range.end(); ++current)
        {
            const size_t nLength(pDocument->getLineLength(side, current.line()));
            const wchar_t* pchData = pDocument->getLineBegin(side, current.line());

            if (current.column() + nSearchLen <= nLength && matches(pchData, nLength, current.column(), strText, bMatchCase))
            {
                bFound = true;
                current = action(current, bStop);
            }
        }

        return bFound;
    }

    template <class Action>
    bool findBackward(Document* pDocument, int side, const std::wstring& findText, bool bMatchCase, const Range& range, Action& action)
    {
        auto found{ false };
        if (pDocument)
        {
            const auto findLength{ findText.length() };
            auto current{ range.end() };
            // This doesn't need to deal with text replacement.
            while (current.offset() > range.begin().offset())
            {
                --current;
                const auto& line{ current.line() };
                if (matches(line.data(), line.length(), current.column(), findText, bMatchCase))
                {
                    found = true;
                    break;
                }
            }
        }
        return found;
    }
}
