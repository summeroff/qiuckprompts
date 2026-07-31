#include "autostart.hpp"
#include "logger.hpp"
#include "util.hpp"
#include "version.hpp"

#include <windows.h>

namespace qp
{
namespace
{

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
// Value name under Run (stable across versions).
constexpr wchar_t kRunValueName[] = L"QiuckPrompts";

std::wstring ParentDirOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return {};
    return path.substr(0, slash);
}

std::wstring LeafName(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return path;
    return path.substr(slash + 1);
}

std::wstring QuoteIfNeeded(const std::wstring& path)
{
    if (path.find(L' ') == std::wstring::npos)
        return path;
    return L"\"" + path + L"\"";
}

} // namespace

std::wstring GetPreferredLaunchExePath()
{
    const std::wstring exe = GetExePath();
    const std::wstring dir = GetExeDir();
    const std::wstring leaf = LeafName(dir);

    // Velopack: …\QiuckPrompts\current\qiuckprompts.exe → stub at …\QiuckPrompts\QiuckPrompts.exe
    if (_wcsicmp(leaf.c_str(), L"current") == 0)
    {
        const std::wstring root = ParentDirOf(dir);
        if (!root.empty())
        {
            const std::wstring stub = PathJoin(root, std::wstring(QP_APP_DISPLAY_W) + L".exe");
            if (FileExists(stub))
                return stub;
            // Some packs use packId.exe lowercase
            const std::wstring stub2 = PathJoin(root, L"qiuckprompts.exe");
            if (FileExists(stub2) && _wcsicmp(stub2.c_str(), exe.c_str()) != 0)
                return stub2;
        }
    }

    // Portable root: Update.exe next to this exe
    if (FileExists(PathJoin(dir, L"Update.exe")))
    {
        const std::wstring stub = PathJoin(dir, std::wstring(QP_APP_DISPLAY_W) + L".exe");
        if (FileExists(stub))
            return stub;
    }

    return exe;
}

bool IsStartWithWindowsEnabled()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    wchar_t buf[1024]{};
    DWORD type = 0;
    DWORD cb = sizeof(buf);
    const LONG rc =
        RegQueryValueExW(key, kRunValueName, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &cb);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) && buf[0] != L'\0';
}

bool SetStartWithWindows(bool enable, std::wstring* error)
{
    return SyncStartWithWindows(enable, error);
}

bool SyncStartWithWindows(bool want, std::wstring* error)
{
    HKEY key = nullptr;
    const LONG openRc = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                                        KEY_SET_VALUE | KEY_QUERY_VALUE, nullptr, &key, nullptr);
    if (openRc != ERROR_SUCCESS)
    {
        if (error)
            *error =
                L"RegCreateKeyEx(Run) failed: " + Win32ErrorMessage(static_cast<DWORD>(openRc));
        return false;
    }

    if (!want)
    {
        const LONG del = RegDeleteValueW(key, kRunValueName);
        RegCloseKey(key);
        if (del != ERROR_SUCCESS && del != ERROR_FILE_NOT_FOUND)
        {
            if (error)
                *error =
                    L"RegDeleteValue(Run) failed: " + Win32ErrorMessage(static_cast<DWORD>(del));
            return false;
        }
        QP_LOG_INFO(L"autostart: disabled (removed HKCU Run\\%s)", kRunValueName);
        return true;
    }

    const std::wstring path = GetPreferredLaunchExePath();
    if (path.empty() || !FileExists(path))
    {
        RegCloseKey(key);
        if (error)
            *error = L"launch exe not found for autostart";
        return false;
    }
    const std::wstring cmd = QuoteIfNeeded(path);
    const DWORD bytes = static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t));
    const LONG setRc = RegSetValueExW(key, kRunValueName, 0, REG_SZ,
                                      reinterpret_cast<const BYTE*>(cmd.c_str()), bytes);
    RegCloseKey(key);
    if (setRc != ERROR_SUCCESS)
    {
        if (error)
            *error = L"RegSetValueEx(Run) failed: " + Win32ErrorMessage(static_cast<DWORD>(setRc));
        return false;
    }
    QP_LOG_INFO(L"autostart: enabled → %s", cmd.c_str());
    return true;
}

} // namespace qp
