#include "title_sample.hpp"
#include "logger.hpp"
#include "util.hpp"

#include <psapi.h>

#include <mutex>
#include <string>
#include <vector>

namespace qp
{

namespace
{

std::mutex g_mu;
std::wstring g_titlesLogPath;

std::wstring WindowTitle(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return {};
    wchar_t buf[512]{};
    GetWindowTextW(hwnd, buf, 512);
    return buf;
}

std::wstring WindowClass(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return {};
    wchar_t buf[256]{};
    GetClassNameW(hwnd, buf, 256);
    return buf;
}

std::wstring ProcessImage(DWORD pid)
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

bool LooksBrowserClass(const std::wstring& cls)
{
    return cls == L"Chrome_WidgetWin_1" || cls == L"Chrome_WidgetWin_0" ||
           cls == L"MozillaWindowClass" || cls == L"ApplicationFrameWindow"; // Edge/Store sometimes
}

void AppendTitlesFile(const std::wstring& line)
{
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_titlesLogPath.empty())
        return;

    // Ensure parent dir
    const size_t slash = g_titlesLogPath.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
    {
        EnsureDirectory(g_titlesLogPath.substr(0, slash));
    }

    HANDLE f =
        CreateFileW(g_titlesLogPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;

    const std::string utf8 = WideToUtf8(line + L"\r\n");
    DWORD written = 0;
    WriteFile(f, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(f);
}

void Emit(const std::wstring& line)
{
    // Stable grep token
    QP_LOG_INFO(L"%s", line.c_str());
    AppendTitlesFile(line);
}

std::wstring FormatSample(const wchar_t* where, HWND hwnd, const std::wstring& note)
{
    SYSTEMTIME st{};
    GetLocalTime(&st);

    const HWND fg = GetForegroundWindow();
    const bool isFg = (hwnd && hwnd == fg);

    DWORD pid = 0;
    if (hwnd)
        GetWindowThreadProcessId(hwnd, &pid);
    const std::wstring title = WindowTitle(hwnd);
    const std::wstring cls = WindowClass(hwnd);
    const std::wstring exe = pid ? FileNameOf(ProcessImage(pid)) : L"";

    wchar_t head[160];
    swprintf(head, 160, L"TITLE_SAMPLE ts=%04u-%02u-%02uT%02u:%02u:%02u.%03u where=%s", st.wYear,
             st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
             where ? where : L"?");

    std::wstring line = head;
    wchar_t mid[128];
    swprintf(mid, 128, L" hwnd=%p fg=%d pid=%lu", hwnd, isFg ? 1 : 0,
             static_cast<unsigned long>(pid));
    line += mid;
    line += L" class='";
    line += cls;
    line += L"' exe='";
    line += exe;
    line += L"' title='";
    line += title;
    line += L"'";
    if (!note.empty())
    {
        line += L" note='";
        line += note;
        line += L"'";
    }
    return line;
}

struct SweepCtx
{
    const wchar_t* where = nullptr;
    int count = 0;
};

BOOL CALLBACK SweepProc(HWND hwnd, LPARAM lp)
{
    auto* ctx = reinterpret_cast<SweepCtx*>(lp);
    if (!IsWindowVisible(hwnd))
        return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr)
        return TRUE;

    LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW)
        return TRUE;

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc))
        return TRUE;
    if ((rc.right - rc.left) < 200 || (rc.bottom - rc.top) < 200)
        return TRUE;

    const std::wstring title = WindowTitle(hwnd);
    if (title.empty())
        return TRUE;

    const std::wstring cls = WindowClass(hwnd);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    const std::wstring exe = FileNameOf(ProcessImage(pid));
    const std::wstring exeL = ToLower(exe);

    const bool browserExe = exeL == L"chrome.exe" || exeL == L"msedge.exe" ||
                            exeL == L"brave.exe" || exeL == L"firefox.exe";
    const bool browserCls = LooksBrowserClass(cls);

    if (!browserExe && !browserCls)
        return TRUE;

    wchar_t note[64];
    swprintf(note, 64, L"sweep#%d", ctx->count);
    Emit(FormatSample(ctx->where, hwnd, note));
    ++ctx->count;
    return TRUE;
}

} // namespace

void SetTitleSampleLogPath(const std::wstring& path)
{
    std::lock_guard<std::mutex> lock(g_mu);
    g_titlesLogPath = path;
    if (!path.empty())
    {
        QP_LOG_INFO(L"TITLE_SAMPLE file=%s", path.c_str());
    }
}

const std::wstring& TitleSampleLogPath()
{
    std::lock_guard<std::mutex> lock(g_mu);
    return g_titlesLogPath;
}

void LogTitleSample(const wchar_t* where, HWND hwnd, const std::wstring& note)
{
    Emit(FormatSample(where, hwnd, note));
}

void LogForegroundTitle(const wchar_t* where, const std::wstring& note)
{
    LogTitleSample(where, GetForegroundWindow(), note);
}

void LogBrowserTitleSweep(const wchar_t* where)
{
    QP_LOG_INFO(L"TITLE_SAMPLE sweep begin where=%s", where ? where : L"?");
    SweepCtx ctx;
    ctx.where = where ? where : L"sweep";
    EnumWindows(SweepProc, reinterpret_cast<LPARAM>(&ctx));
    // Always include pure foreground even if not classified as browser
    LogForegroundTitle(where, L"foreground");
    QP_LOG_INFO(L"TITLE_SAMPLE sweep end where=%s browsers=%d", where ? where : L"?", ctx.count);
}

} // namespace qp
