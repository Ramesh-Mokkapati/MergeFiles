// TextSearch.cpp - finding text in a document, line by line.
#include "TextSearch.h"

#include <cassert>
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
        assert(pDocument);

        bool bFound(false), bStop(false);
        const size_t nSearchLen(strText.length());

        for (LineIterator<Document> current(range.begin(), pDocument, side); !bStop && current < range.end(); ++current)
        {
            const int nLength(pDocument->getLineLength(side, current.line()));
            const wchar_t* pchOtherData = pDocument->getLineData(1 - side, current.line());
            const wchar_t* pchData = pDocument->getLineData(side, current.line());

            if (current.column() + nSearchLen <= nLength && matches(pchData, nLength, current.column(), strText, bMatchCase))
                bFound = true;
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
            // This does not need to deal with text replacement.
            while (current.offset() > range.begin().offset())
            {
                --current;
                const auto& line{ current.line() };
                const auto prunedLength{ line.getPrunedLength() };
                if (matches(line.data(), prunedLength, current.column(), findText, bMatchCase))
                {
                    found = true;
                    break;
                }
            }
        }
        return found;
    }
}
