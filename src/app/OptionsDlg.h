// OptionsDlg.h - the Options dialog: comparison settings, rules (line, block and pairing
// expressions), and display (font, wrapping, padding, colours).
#pragma once

#include "framework.h"

#include "CompareWnd.h"
#include "FolderCompare.h"
#include "Theme.h"

namespace mf
{
    struct OptionsData
    {
        CompareOptions compare;
        ViewSettings view;
        FolderOptions folders;
        int imageTolerance = 0;
        std::wstring fontFace;
        int fontSize = 10;
        Theme theme;
    };

    // Shows the dialog; true (and data updated) when the user pressed OK.
    bool ShowOptionsDialog(CWnd* parent, OptionsData& data);
}
