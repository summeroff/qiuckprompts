#pragma once

#include <windows.h>

#include <string>

namespace qp
{

struct BrowserTarget
{
    HWND hwnd = nullptr;
    DWORD pid = 0;
    std::wstring title;
    std::wstring exePath;
    int score = 0;
};

// Find best visible top-level browser window.
// Prefers Chrome Dev / Beta (install path), then any Chrome, then Edge,
// then Brave, then Firefox. titleHint e.g. L"Chrome Dev" (case-insensitive
// substring on title/path), may be empty.
bool FindBrowserWindow(const std::wstring& titleHint, BrowserTarget& out,
                       std::wstring* error = nullptr);

// Activate + restore. Uses ForceForeground.
bool ActivateBrowser(const BrowserTarget& target, std::wstring* error = nullptr);

} // namespace qp
