#include "util.hpp"
#include "version.hpp"

#include <shellapi.h>

#include <cctype>
#include <cstdio>
#include <vector>

namespace qp {

std::wstring GetExePath() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD n = GetModuleFileNameW(nullptr, path.data(), MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        // Grow buffer for long paths
        path.resize(32768, L'\0');
        n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0 || n >= path.size()) {
            return {};
        }
    }
    path.resize(n);
    return path;
}

std::wstring GetExeDir() {
    std::wstring path = GetExePath();
    const size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        return L".";
    }
    return path.substr(0, pos);
}

std::wstring GetAppDataDir(bool ensure) {
    DWORD needed = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    std::wstring root;
    if (needed > 1) {
        root.resize(needed);
        GetEnvironmentVariableW(L"LOCALAPPDATA", root.data(), needed);
        if (!root.empty() && root.back() == L'\0') {
            root.pop_back();
        }
    }
    if (root.empty()) {
        root = GetExeDir();
    }
    std::wstring dir = PathJoin(root, QP_APP_NAME_W);
    if (ensure) {
        EnsureDirectory(dir);
    }
    return dir;
}

std::wstring PathJoin(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const bool aSlash = a.back() == L'\\' || a.back() == L'/';
    const bool bSlash = b.front() == L'\\' || b.front() == L'/';
    if (aSlash && bSlash) return a + b.substr(1);
    if (!aSlash && !bSlash) return a + L"\\" + b;
    return a + b;
}

std::wstring PathJoin(std::initializer_list<std::wstring> parts) {
    std::wstring out;
    for (const auto& p : parts) {
        if (p.empty()) continue;
        out = out.empty() ? p : PathJoin(out, p);
    }
    return out;
}

bool DirectoryExists(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool FileExists(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool EnsureDirectory(const std::wstring& path) {
    if (path.empty()) return false;
    if (DirectoryExists(path)) return true;

    // Create parents recursively
    std::wstring cur;
    for (size_t i = 0; i < path.size(); ++i) {
        const wchar_t c = path[i];
        cur.push_back(c);
        if (c == L'\\' || c == L'/' || i + 1 == path.size()) {
            if (cur.size() == 2 && cur[1] == L':') continue; // "C:"
            if (cur == L"\\" || cur == L"/") continue;
            if (!DirectoryExists(cur)) {
                if (!CreateDirectoryW(cur.c_str(), nullptr)) {
                    const DWORD err = GetLastError();
                    if (err != ERROR_ALREADY_EXISTS) {
                        return false;
                    }
                }
            }
        }
    }
    return DirectoryExists(path);
}

bool OpenInExplorer(const std::wstring& path) {
    if (path.empty()) return false;
    INT_PTR rc;
    if (DirectoryExists(path)) {
        rc = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(),
                                                     nullptr, nullptr, SW_SHOWNORMAL));
    } else {
        std::wstring args = L"/select,\"" + path + L"\"";
        rc = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", L"explorer.exe",
                                                     args.c_str(), nullptr, SW_SHOWNORMAL));
    }
    return rc > 32;
}

bool OpenTextFile(const std::wstring& path) {
    if (path.empty() || !FileExists(path)) return false;
    INT_PTR rc = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return rc > 32;
}

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                      static_cast<int>(utf8.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                        out.data(), n);
    return out;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                      static_cast<int>(wide.size()),
                                      nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                        out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Trim(const std::wstring& s) {
    size_t b = 0;
    while (b < s.size() && iswspace(s[b])) ++b;
    size_t e = s.size();
    while (e > b && iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::wstring ToLower(const std::wstring& s) {
    std::wstring out = s;
    for (auto& c : out) {
        c = static_cast<wchar_t>(towlower(c));
    }
    return out;
}

std::wstring FormatHotkeyDisplay(UINT modifiers, UINT vk) {
    std::wstring s;
    // Strip MOD_NOREPEAT for display
    const UINT m = modifiers & ~(static_cast<UINT>(MOD_NOREPEAT));
    if (m & MOD_CONTROL) s += L"Ctrl+";
    if (m & MOD_ALT)     s += L"Alt+";
    if (m & MOD_SHIFT)   s += L"Shift+";
    if (m & MOD_WIN)     s += L"Win+";

    if (vk >= '0' && vk <= '9') {
        s.push_back(static_cast<wchar_t>(vk));
    } else if (vk >= 'A' && vk <= 'Z') {
        s.push_back(static_cast<wchar_t>(vk));
    } else if (vk >= VK_F1 && vk <= VK_F24) {
        s += L"F";
        s += std::to_wstring(vk - VK_F1 + 1);
    } else {
        wchar_t buf[32];
        swprintf(buf, 32, L"VK_0x%02X", vk);
        s += buf;
    }
    return s;
}

std::wstring Win32ErrorMessage(DWORD code) {
    wchar_t* buf = nullptr;
    const DWORD n = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring msg;
    if (n && buf) {
        msg.assign(buf, n);
        while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n' || msg.back() == L' ')) {
            msg.pop_back();
        }
        LocalFree(buf);
    } else {
        msg = L"(no message)";
    }
    wchar_t prefix[32];
    swprintf(prefix, 32, L"[%lu] ", static_cast<unsigned long>(code));
    return std::wstring(prefix) + msg;
}

std::wstring LastErrorMessage() {
    return Win32ErrorMessage(GetLastError());
}

SingleInstance::~SingleInstance() {
    if (mutex_) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
    }
}

bool SingleInstance::Acquire(const std::wstring& name) {
    SetLastError(0);
    mutex_ = CreateMutexW(nullptr, TRUE, name.c_str());
    if (!mutex_) return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex_);
        mutex_ = nullptr;
        return false;
    }
    return true;
}

} // namespace qp
