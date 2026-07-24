#pragma once

#include <windows.h>

#include <string>

namespace qp {

struct BrowserTarget {
    HWND hwnd = nullptr;
    DWORD pid = 0;
    std::wstring title;
    std::wstring exePath;
    int score = 0;
};

// Find best visible top-level browser window.
// Prefers Chrome Beta (path/title), then any Chrome, then Edge.
// titleHint e.g. L"Chrome Beta" (case-insensitive substring), may be empty.
bool FindBrowserWindow(const std::wstring& titleHint,
                       BrowserTarget& out,
                       std::wstring* error = nullptr);

// Activate + restore. Uses ForceForeground.
bool ActivateBrowser(const BrowserTarget& target, std::wstring* error = nullptr);

} // namespace qp
