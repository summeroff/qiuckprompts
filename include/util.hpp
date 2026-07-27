#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <windows.h>

namespace qp
{

// Paths
std::wstring GetExeDir();
std::wstring GetExePath();
std::wstring GetAppDataDir(bool ensure = true);
std::wstring PathJoin(const std::wstring& a, const std::wstring& b);
std::wstring PathJoin(std::initializer_list<std::wstring> parts);

bool DirectoryExists(const std::wstring& path);
bool FileExists(const std::wstring& path);
bool EnsureDirectory(const std::wstring& path);
bool OpenInExplorer(const std::wstring& path);
bool OpenTextFile(const std::wstring& path);

// UTF-8 / wide
std::wstring Utf8ToWide(const std::string& utf8);
std::string WideToUtf8(const std::wstring& wide);

std::wstring Trim(const std::wstring& s);
std::wstring ToLower(const std::wstring& s);

// Hotkey
struct HotkeySpec
{
    UINT modifiers = 0;   // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN [| MOD_NOREPEAT]
    UINT vk = 0;          // virtual-key code
    std::wstring display; // "Ctrl+Alt+1"
};

std::wstring FormatHotkeyDisplay(UINT modifiers, UINT vk);

// Errors
std::wstring Win32ErrorMessage(DWORD code);
std::wstring LastErrorMessage();

// Single-instance gate for one interactive user session (Local\ namespace).
//
//   Named mutex  — exclusive ownership (only one live app).
//   Named event  — "please exit" signal for takeover (manual-reset).
//                  Prefer event over semaphore: one sticky signal until the
//                  new owner resets it; works even if the old instance has not
//                  started its watcher yet.
//
// Second launch: caller shows UI, then AcquireOrTakeOver(..., takeOver=true)
// sets the event and waits for the mutex (old process WM_CLOSE → exit).
class SingleInstance
{
public:
    SingleInstance() = default;
    ~SingleInstance();
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    // Try to own the mutex. If another instance holds it:
    //   takeOver=false → return false immediately (caller shows Yes/No UI).
    //   takeOver=true  → SetEvent(shutdown) + optional FindWindow/WM_CLOSE, then wait.
    // peerWindowClass: top-level window class of the running app (backup signal path).
    bool AcquireOrTakeOver(const std::wstring& mutexName, const std::wstring& shutdownEventName,
                           bool takeOver, DWORD timeoutMs, std::wstring* error = nullptr,
                           const wchar_t* peerWindowClass = nullptr);

    // True if another instance currently owns the mutex (does not take ownership).
    static bool IsAnotherRunning(const std::wstring& mutexName);

    // After the message HWND exists: watch shutdown event → PostMessage WM_CLOSE.
    bool StartShutdownWatcher(HWND hwnd, std::wstring* error = nullptr);

private:
    static DWORD WINAPI WatcherThreadMain(void* param);
    void StopWatcher();
    void CloseAll();

    HANDLE mutex_ = nullptr;
    HANDLE shutdownEvent_ = nullptr; // named, manual-reset
    HANDLE stopEvent_ = nullptr;     // local manual-reset — stop watcher thread
    HANDLE watcherThread_ = nullptr;
    HWND watchHwnd_ = nullptr;
    bool ownsMutex_ = false;
};

} // namespace qp
