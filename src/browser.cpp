#include "browser.hpp"
#include "logger.hpp"
#include "input_sim.hpp"
#include "title_sample.hpp"
#include "util.hpp"

#include <psapi.h>

#include <vector>

namespace qp
{

namespace
{

std::wstring ToLowerCopy(std::wstring s)
{
    for (auto& c : s)
        c = static_cast<wchar_t>(towlower(c));
    return s;
}

std::wstring ProcessImagePath(DWORD pid)
{
    std::wstring path;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return path;
    wchar_t buf[4096];
    DWORD size = static_cast<DWORD>(sizeof(buf) / sizeof(buf[0]));
    if (QueryFullProcessImageNameW(h, 0, buf, &size))
    {
        path.assign(buf, size);
    }
    CloseHandle(h);
    return path;
}

std::wstring FileNameOf(const std::wstring& path)
{
    const size_t p = path.find_last_of(L"\\/");
    if (p == std::wstring::npos)
        return path;
    return path.substr(p + 1);
}

bool IsBrowserExe(const std::wstring& fileLower)
{
    return fileLower == L"chrome.exe" || fileLower == L"msedge.exe" || fileLower == L"brave.exe" ||
           fileLower == L"firefox.exe";
}

int ScoreBrowser(const std::wstring& title, const std::wstring& exePath,
                 const std::wstring& titleHintLower)
{
    const std::wstring titleL = ToLowerCopy(title);
    const std::wstring pathL = ToLowerCopy(exePath);
    const std::wstring fileL = ToLowerCopy(FileNameOf(exePath));

    if (!IsBrowserExe(fileL))
        return -1;

    int score = 0;

    // Process preference
    if (fileL == L"chrome.exe")
        score += 30;
    else if (fileL == L"msedge.exe")
        score += 15;
    else if (fileL == L"brave.exe")
        score += 10;
    else if (fileL == L"firefox.exe")
        score += 5;

    // Channel install dirs: "...\Chrome Dev\...", "...\Chrome Beta\...", "...\Chrome\..."
    // Titles usually stay "… - Google Chrome" for all channels — path is the real signal.
    if (pathL.find(L"chrome dev") != std::wstring::npos)
        score += 100;
    else if (pathL.find(L"chrome beta") != std::wstring::npos)
        score += 100;

    if (titleL.find(L"chrome dev") != std::wstring::npos)
        score += 80;
    else if (titleL.find(L"chrome beta") != std::wstring::npos)
        score += 80;

    if (!titleHintLower.empty())
    {
        if (titleL.find(titleHintLower) != std::wstring::npos)
            score += 60;
        if (pathL.find(titleHintLower) != std::wstring::npos)
            score += 60;
    }

    // Prefer real content windows (have a non-empty title, not tiny)
    if (!title.empty())
        score += 5;

    return score;
}

// Secondary rank when scores tie (Dev > Beta > stable Chrome > other).
int ChannelRank(const std::wstring& exePath)
{
    const std::wstring pathL = ToLowerCopy(exePath);
    if (pathL.find(L"chrome dev") != std::wstring::npos)
        return 3;
    if (pathL.find(L"chrome beta") != std::wstring::npos)
        return 2;
    if (pathL.find(L"\\chrome\\") != std::wstring::npos ||
        pathL.find(L"/chrome/") != std::wstring::npos)
        return 1;
    return 0;
}

struct EnumCtx
{
    std::wstring hintLower;
    std::vector<BrowserTarget> found;
};

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp)
{
    auto* ctx = reinterpret_cast<EnumCtx*>(lp);
    if (!IsWindowVisible(hwnd))
        return TRUE;

    // Skip owned tool windows / child popups without caption where possible
    if (GetWindow(hwnd, GW_OWNER) != nullptr)
        return TRUE;

    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW)
        return TRUE;

    wchar_t cls[128]{};
    GetClassNameW(hwnd, cls, 128);
    // Chrome/Edge/Brave main windows
    const bool chromeFamily =
        wcscmp(cls, L"Chrome_WidgetWin_1") == 0 || wcscmp(cls, L"Chrome_WidgetWin_0") == 0;
    const bool firefox = wcscmp(cls, L"MozillaWindowClass") == 0;
    if (!chromeFamily && !firefox)
        return TRUE;

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc))
        return TRUE;
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    if (w < 200 || h < 200)
        return TRUE; // skip ghost/extension HWNDs

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid)
        return TRUE;

    wchar_t titleBuf[512]{};
    GetWindowTextW(hwnd, titleBuf, 512);
    const std::wstring title = titleBuf;
    // Empty title chrome windows are often background helpers
    if (title.empty())
        return TRUE;

    const std::wstring exe = ProcessImagePath(pid);
    const int score = ScoreBrowser(title, exe, ctx->hintLower);
    if (score < 0)
        return TRUE;

    BrowserTarget t;
    t.hwnd = hwnd;
    t.pid = pid;
    t.title = title;
    t.exePath = exe;
    t.score = score;
    ctx->found.push_back(std::move(t));
    return TRUE;
}

} // namespace

bool FindBrowserWindow(const std::wstring& titleHint, BrowserTarget& out, std::wstring* error)
{
    EnumCtx ctx;
    ctx.hintLower = ToLowerCopy(titleHint);

    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&ctx));

    if (ctx.found.empty())
    {
        if (error)
        {
            *error = L"No browser window found";
            if (!titleHint.empty())
            {
                *error += L" (hint='" + titleHint + L"')";
            }
            *error += L". Open Chrome Dev/Beta (or Chrome/Edge) and try again.";
        }
        QP_LOG_ERROR(L"browser: no candidates (hint='%s')", titleHint.c_str());
        return false;
    }

    BrowserTarget* best = &ctx.found[0];
    for (auto& t : ctx.found)
    {
        QP_LOG_DEBUG(L"browser candidate score=%d hwnd=%p pid=%lu title='%s' exe='%s'", t.score,
                     t.hwnd, static_cast<unsigned long>(t.pid), t.title.c_str(), t.exePath.c_str());
        // Stable samples for config mining (every candidate).
        wchar_t note[64];
        swprintf(note, 64, L"score=%d", t.score);
        LogTitleSample(L"browser_candidate", t.hwnd, note);
        if (t.score > best->score ||
            (t.score == best->score && ChannelRank(t.exePath) > ChannelRank(best->exePath)))
            best = &t;
    }

    out = *best;
    QP_LOG_INFO(L"browser: selected score=%d hwnd=%p title='%s' exe='%s'", out.score, out.hwnd,
                out.title.c_str(), out.exePath.c_str());
    LogTitleSample(L"browser_selected", out.hwnd, out.exePath);
    return true;
}

bool ActivateBrowser(const BrowserTarget& target, std::wstring* error)
{
    if (!target.hwnd)
    {
        if (error)
            *error = L"null browser hwnd";
        return false;
    }
    return ForceForeground(target.hwnd, error);
}

} // namespace qp
