// framework.h - MFC includes shared by the GUI sources (the engine sources don't use MFC).
#pragma once

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <sdkddkver.h>

#include <afxwin.h>     // MFC core and standard components
#include <afxext.h>     // MFC extensions (CStatusBar, CControlBar)
#include <afxcmn.h>     // common controls (CToolTipCtrl)
#include <afxdlgs.h>    // CFileDialog
#include <imm.h>        // IME composition window position

#include <algorithm>
#include <memory>
#include <string>
#include <vector>
