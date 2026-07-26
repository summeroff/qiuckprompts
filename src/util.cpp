#include "util.hpp"
#include "version.hpp"

#include <shellapi.h>

#include <cctype>
#include <cstdio>
#include <vector>

namespace qp
{

std::wstring GetExePath()
{
    std::wstring path(MAX_PATH, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, path.data(), MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
    {
        // Grow buffer for long paths
        path.resize(32768, L'\0');
        n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0 || n >= path.size())
        {
            return {};
        }
    }
    path.resize(n);
    return path;
}

std::wstring GetExeDir()
{
    std::wstring path = GetExePath();
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
    {
        return L".";
    }
    return path.substr(0, pos);
}

std::wstring GetAppDataDir(bool ensure)
{
    DWORD needed = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    std::wstring root;
    if (needed > 1)
    {
        root.resize(needed);
        GetEnvironmentVariableW(L"LOCALAPPDATA", root.data(), needed);
        if (!root.empty() && root.back() == L'\0')
        {
            root.pop_back();
        }
    }
    if (root.empty())
    {
        root = GetExeDir();
    }
    std::wstring dir = PathJoin(root, QP_APP_NAME_W);
    if (ensure)
    {
        EnsureDirectory(dir);
    }
    return dir;
}

std::wstring PathJoin(const std::wstring& a, const std::wstring& b)
{
    if (a.empty())
        return b;
    if (b.empty())
        return a;
    const bool aSlash = a.back() == L'\\' || a.back() == L'/';
    const bool bSlash = b.front() == L'\\' || b.front() == L'/';
    if (aSlash && bSlash)
        return a + b.substr(1);
    if (!aSlash && !bSlash)
        return a + L"\\" + b;
    return a + b;
}

std::wstring PathJoin(std::initializer_list<std::wstring> parts)
{
    std::wstring out;
    for (const auto& p : parts)
    {
        if (p.empty())
            continue;
        out = out.empty() ? p : PathJoin(out, p);
    }
    return out;
}

bool DirectoryExists(const std::wstring& path)
{
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool FileExists(const std::wstring& path)
{
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool EnsureDirectory(const std::wstring& path)
{
    if (path.empty())
        return false;
    if (DirectoryExists(path))
        return true;

    // Create parents recursively
    std::wstring cur;
    for (size_t i = 0; i < path.size(); ++i)
    {
        const wchar_t c = path[i];
        cur.push_back(c);
        if (c == L'\\' || c == L'/' || i + 1 == path.size())
        {
            if (cur.size() == 2 && cur[1] == L':')
                continue; // "C:"
            if (cur == L"\\" || cur == L"/")
                continue;
            if (!DirectoryExists(cur))
            {
                if (!CreateDirectoryW(cur.c_str(), nullptr))
                {
                    const DWORD err = GetLastError();
                    if (err != ERROR_ALREADY_EXISTS)
                    {
                        return false;
                    }
                }
            }
        }
    }
    return DirectoryExists(path);
}

bool OpenInExplorer(const std::wstring& path)
{
    if (path.empty())
        return false;
    INT_PTR rc;
    if (DirectoryExists(path))
    {
        rc = reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    } else
    {
        std::wstring args = L"/select,\"" + path + L"\"";
        rc = reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL));
    }
    return rc > 32;
}

bool OpenTextFile(const std::wstring& path)
{
    if (path.empty() || !FileExists(path))
        return false;
    INT_PTR rc = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return rc > 32;
}

std::wstring Utf8ToWide(const std::string& utf8)
{
    if (utf8.empty())
        return {};
    const int n =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0)
        return {};
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string WideToUtf8(const std::wstring& wide)
{
    if (wide.empty())
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n,
                        nullptr, nullptr);
    return out;
}

std::wstring Trim(const std::wstring& s)
{
    size_t b = 0;
    while (b < s.size() && iswspace(s[b]))
        ++b;
    size_t e = s.size();
    while (e > b && iswspace(s[e - 1]))
        --e;
    return s.substr(b, e - b);
}

std::wstring ToLower(const std::wstring& s)
{
    std::wstring out = s;
    for (auto& c : out)
    {
        c = static_cast<wchar_t>(towlower(c));
    }
    return out;
}

std::wstring FormatHotkeyDisplay(UINT modifiers, UINT vk)
{
    std::wstring s;
    // Strip MOD_NOREPEAT for display
    const UINT m = modifiers & ~(static_cast<UINT>(MOD_NOREPEAT));
    if (m & MOD_CONTROL)
        s += L"Ctrl+";
    if (m & MOD_ALT)
        s += L"Alt+";
    if (m & MOD_SHIFT)
        s += L"Shift+";
    if (m & MOD_WIN)
        s += L"Win+";

    if (vk >= '0' && vk <= '9')
    {
        s.push_back(static_cast<wchar_t>(vk));
    } else if (vk >= 'A' && vk <= 'Z')
    {
        s.push_back(static_cast<wchar_t>(vk));
    } else if (vk >= VK_F1 && vk <= VK_F24)
    {
        s += L"F";
        s += std::to_wstring(vk - VK_F1 + 1);
    } else
    {
        wchar_t buf[32];
        swprintf(buf, 32, L"VK_0x%02X", vk);
        s += buf;
    }
    return s;
}

std::wstring Win32ErrorMessage(DWORD code)
{
    wchar_t* buf = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                       FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                   reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring msg;
    if (n && buf)
    {
        msg.assign(buf, n);
        while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n' || msg.back() == L' '))
        {
            msg.pop_back();
        }
        LocalFree(buf);
    } else
    {
        msg = L"(no message)";
    }
    wchar_t prefix[32];
    swprintf(prefix, 32, L"[%lu] ", static_cast<unsigned long>(code));
    return std::wstring(prefix) + msg;
}

std::wstring LastErrorMessage()
{
    return Win32ErrorMessage(GetLastError());
}

SingleInstance::~SingleInstance()
{
    CloseAll();
}

void SingleInstance::CloseAll()
{
    StopWatcher();
    if (ownsMutex_ && mutex_)
    {
        ReleaseMutex(mutex_);
        ownsMutex_ = false;
    }
    if (mutex_)
    {
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
    if (shutdownEvent_)
    {
        CloseHandle(shutdownEvent_);
        shutdownEvent_ = nullptr;
    }
    if (stopEvent_)
    {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
}

void SingleInstance::StopWatcher()
{
    if (stopEvent_)
        SetEvent(stopEvent_);
    if (watcherThread_)
    {
        const DWORD w = WaitForSingleObject(watcherThread_, 3000);
        if (w == WAIT_TIMEOUT)
            TerminateThread(watcherThread_, 1); // last resort; process is exiting
        CloseHandle(watcherThread_);
        watcherThread_ = nullptr;
    }
    watchHwnd_ = nullptr;
}

bool SingleInstance::IsAnotherRunning(const std::wstring& mutexName)
{
    SetLastError(0);
    HANDLE m = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (!m)
        return false;
    const bool exists = (GetLastError() == ERROR_ALREADY_EXISTS);
    // If we created it, we must not leave a phantom owner — close immediately.
    // Creating with bInitialOwner=FALSE does not take ownership until Wait.
    CloseHandle(m);
    return exists;
}

bool SingleInstance::AcquireOrTakeOver(const std::wstring& mutexName,
                                       const std::wstring& shutdownEventName, bool takeOver,
                                       DWORD timeoutMs, std::wstring* error,
                                       const wchar_t* peerWindowClass)
{
    CloseAll();

    SetLastError(0);
    mutex_ = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    if (!mutex_)
    {
        if (error)
            *error = L"CreateMutex failed: " + LastErrorMessage();
        return false;
    }

    // Named manual-reset event: sticky "please quit" until the new owner resets.
    SetLastError(0);
    shutdownEvent_ =
        CreateEventW(nullptr, TRUE /*manual*/, FALSE /*nonsignaled*/, shutdownEventName.c_str());
    if (!shutdownEvent_)
    {
        if (error)
            *error = L"CreateEvent(shutdown) failed: " + LastErrorMessage();
        CloseHandle(mutex_);
        mutex_ = nullptr;
        return false;
    }

    // Try immediate ownership.
    DWORD wr = WaitForSingleObject(mutex_, 0);
    if (wr == WAIT_OBJECT_0 || wr == WAIT_ABANDONED)
    {
        ownsMutex_ = true;
        // Clear any leftover takeover signal so we don't quit ourselves.
        ResetEvent(shutdownEvent_);
        return true;
    }

    if (wr != WAIT_TIMEOUT)
    {
        if (error)
            *error = L"WaitForSingleObject(mutex) failed: " + Win32ErrorMessage(wr);
        CloseAll();
        return false;
    }

    // Another instance owns the mutex.
    if (!takeOver)
    {
        CloseAll();
        return false;
    }

    if (!SetEvent(shutdownEvent_))
    {
        if (error)
            *error = L"SetEvent(shutdown) failed: " + LastErrorMessage();
        CloseAll();
        return false;
    }

    // Backup path: PostMessage WM_CLOSE to the peer's hidden top-level window.
    // (Event watcher is primary; this helps if the watcher thread is stuck.)
    if (peerWindowClass && peerWindowClass[0])
    {
        HWND peer = FindWindowW(peerWindowClass, nullptr);
        if (peer)
            PostMessageW(peer, WM_CLOSE, 0, 0);
    }

    wr = WaitForSingleObject(mutex_, timeoutMs);
    if (wr != WAIT_OBJECT_0 && wr != WAIT_ABANDONED)
    {
        if (error)
        {
            if (wr == WAIT_TIMEOUT)
                *error = L"Timed out waiting for the other QiuckPrompts instance to exit.";
            else
                *error = L"WaitForSingleObject(mutex takeover) failed: " + Win32ErrorMessage(wr);
        }
        CloseAll();
        return false;
    }

    ownsMutex_ = true;
    ResetEvent(shutdownEvent_);
    return true;
}

DWORD WINAPI SingleInstance::WatcherThreadMain(void* param)
{
    auto* self = static_cast<SingleInstance*>(param);
    if (!self || !self->shutdownEvent_ || !self->stopEvent_)
        return 0;

    HANDLE waits[2] = {self->stopEvent_, self->shutdownEvent_};
    for (;;)
    {
        const DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (w == WAIT_OBJECT_0)
        {
            // stopEvent — exit thread
            break;
        }
        if (w == WAIT_OBJECT_0 + 1)
        {
            const HWND hwnd = self->watchHwnd_;
            if (hwnd && IsWindow(hwnd))
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
            // Manual-reset stays signaled; avoid a tight PostMessage loop.
            // Sleep until stop or a short poll — process should be exiting.
            for (;;)
            {
                const DWORD s = WaitForSingleObject(self->stopEvent_, 50);
                if (s == WAIT_OBJECT_0)
                    return 0;
                if (!self->watchHwnd_ || !IsWindow(self->watchHwnd_))
                    return 0;
            }
        }
        break;
    }
    return 0;
}

bool SingleInstance::StartShutdownWatcher(HWND hwnd, std::wstring* error)
{
    if (!hwnd)
    {
        if (error)
            *error = L"StartShutdownWatcher: null hwnd";
        return false;
    }
    if (!shutdownEvent_)
    {
        if (error)
            *error = L"StartShutdownWatcher: no shutdown event (Acquire first)";
        return false;
    }

    StopWatcher();
    watchHwnd_ = hwnd;

    stopEvent_ = CreateEventW(nullptr, TRUE /*manual*/, FALSE, nullptr);
    if (!stopEvent_)
    {
        if (error)
            *error = L"CreateEvent(stop) failed: " + LastErrorMessage();
        return false;
    }

    watcherThread_ = CreateThread(nullptr, 0, &SingleInstance::WatcherThreadMain, this, 0, nullptr);
    if (!watcherThread_)
    {
        if (error)
            *error = L"CreateThread(shutdown watcher) failed: " + LastErrorMessage();
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
        watchHwnd_ = nullptr;
        return false;
    }
    return true;
}

} // namespace qp
