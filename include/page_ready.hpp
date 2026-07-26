#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace qp
{

struct PageReadyConfig
{
    HWND browserHwnd = nullptr;

    // If non-empty, window title must contain this (case-insensitive), e.g. L"Meta".
    // Empty => any title that doesn't look like a blank/new tab is OK.
    std::wstring titleHint;

    // Reject titles that mean "not navigated yet".
    std::vector<std::wstring> titleReject = {
        L"New Tab", L"new tab", L"Untitled", L"about:blank", L"Chrome Beta",
        // bare browser name only (no page title yet) — kept loose; combined with other checks
    };

    int timeoutMs = 15000; // hard stop
    int pollMs = 150;      // poll interval
    int minWaitMs = 400;   // never paste sooner than this after Enter
    int settleMs = 200;    // after ready signal, tiny settle before paste

    bool useUia = true; // UI Automation tree (sees web edits; not real HWNDs)
    bool preferFocusedEdit = true;
    bool focusFoundEdit = true;
};

struct PageReadyResult
{
    bool ready = false;
    bool usedUia = false;
    bool focusedEdit = false;
    std::wstring title;
    std::wstring editName; // UIA Name of edit, if any
    std::wstring detail;   // human reason for logs
    int waitedMs = 0;
};

// Guess a title substring from the AI URL (meta.ai → "Meta", etc.).
std::wstring TitleHintFromUrl(const std::wstring& url);

// Poll until the AI page looks ready, or timeout.
// IMPORTANT: Chrome does NOT expose DOM inputs as Win32 HWNDs. The page lives
// inside Chrome_RenderWidgetHostHWND. We use:
//   1) window title heuristics (navigation progressed)
//   2) UI Automation accessibility tree (find Edit control, optional SetFocus)
bool WaitForAiPageReady(const PageReadyConfig& cfg, PageReadyResult& out,
                        std::wstring* error = nullptr);

// One-shot probe (for debug / self-test).
bool UiaFindChatEdit(HWND browserHwnd, std::wstring* editName, bool setFocus,
                     std::wstring* error = nullptr);

// Process-wide COM init for UIA (safe to call multiple times).
bool EnsureComInitialized();

} // namespace qp
