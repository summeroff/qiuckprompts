#pragma once

#include <windows.h>

#include <string>

namespace qp {

// Collect real window titles for future config (pageTitleHint, browser matching).
// Lines are written to:
//   1) main logger at INFO with prefix TITLE_SAMPLE
//   2) a dedicated titles.log next to the main log (easy to mine later)
//
// Keep the "where=" tags stable — they are the query key when grepping.

void SetTitleSampleLogPath(const std::wstring& path);
const std::wstring& TitleSampleLogPath();

// Sample one hwnd (title, class, pid, whether it is foreground).
void LogTitleSample(const wchar_t* where, HWND hwnd,
                    const std::wstring& note = {});

// Sample current foreground window.
void LogForegroundTitle(const wchar_t* where, const std::wstring& note = {});

// Enumerate visible top-level windows that look like browsers + foreground.
// Useful after you open meta.ai / gemini manually a few times.
void LogBrowserTitleSweep(const wchar_t* where);

} // namespace qp
