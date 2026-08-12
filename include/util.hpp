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
// %LOCALAPPDATA%\QiuckPrompts — config, logs, backups, native-messaging host files.
std::wstring GetAppDataDir(bool ensure = true);
std::wstring GetUserConfigPath();
std::wstring GetUserLogsDir(bool ensure = true);
std::wstring GetUserBackupsDir(bool ensure = true);
std::wstring GetInstallConfigTemplatePath();
std::wstring PathJoin(const std::wstring& a, const std::wstring& b);
std::wstring PathJoin(std::initializer_list<std::wstring> parts);

bool DirectoryExists(const std::wstring& path);
bool FileExists(const std::wstring& path);
bool EnsureDirectory(const std::wstring& path);
bool OpenInExplorer(const std::wstring& path);
bool OpenTextFile(const std::wstring& path);
bool CopyFilePath(const std::wstring& src, const std::wstring& dst, bool failIfExists = false);
// Timestamped copy under backups\; keeps newest maxKeep (default 5).
bool BackupFileToUserBackups(const std::wstring& srcPath, int maxKeep = 5,
                             std::wstring* backupPathOut = nullptr);
// Seed AppData ini from install template (or migrate exe-adjacent). Does not overwrite
// an existing user ini. Returns resolved user config path.
bool EnsureUserConfigFile(std::wstring* resolvedPath, std::wstring* error = nullptr);
// Rotate log-style files: name.log -> name.1.log ... keep maxFiles-1 older copies.
bool RotateLogFile(const std::wstring& logPath, int maxFiles = 4);

// UTF-8 / wide
std::wstring Utf8ToWide(const std::string& utf8);
std::string WideToUtf8(const std::wstring& wide);

std::wstring Trim(const std::wstring& s);
std::wstring ToLower(const std::wstring& s);

// True iff url is https:// with a non-empty host. No DNS. Rejects http, file,
// javascript, and empty/whitespace hosts.
bool IsHttpsUrl(const std::wstring& url);

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
