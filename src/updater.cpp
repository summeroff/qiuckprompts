#include "updater.hpp"
#include "autostart.hpp"
#include "ext_bridge.hpp"
#include "logger.hpp"
#include "util.hpp"
#include "version.hpp"

#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace qp
{
namespace
{

std::wstring ParentDir(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return {};
    return path.substr(0, slash);
}

std::wstring FileNameOf(const std::wstring& path)
{
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return path;
    return path.substr(slash + 1);
}

// exe in ...\current\qiuckprompts.exe → root is parent of current
// Update.exe is at root\Update.exe
std::wstring GuessInstallRoot()
{
    const std::wstring exeDir = GetExeDir();
    const std::wstring leaf = FileNameOf(exeDir);
    if (_wcsicmp(leaf.c_str(), L"current") == 0)
        return ParentDir(exeDir);
    // Portable/stub layout sometimes runs from root via stub; also check sibling Update.exe
    if (FileExists(PathJoin(exeDir, L"Update.exe")))
        return exeDir;
    const std::wstring parent = ParentDir(exeDir);
    if (!parent.empty() && FileExists(PathJoin(parent, L"Update.exe")))
        return parent;
    return {};
}

bool ParseUrl(const std::wstring& url, std::wstring& host, std::wstring& path, INTERNET_PORT& port,
              bool& https)
{
    host.clear();
    path.clear();
    port = 0;
    https = false;
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t hostBuf[256]{};
    wchar_t pathBuf[2048]{};
    uc.lpszHostName = hostBuf;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = pathBuf;
    uc.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc))
        return false;
    host.assign(hostBuf, uc.dwHostNameLength);
    path.assign(pathBuf, uc.dwUrlPathLength);
    if (path.empty())
        path = L"/";
    https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    port = uc.nPort;
    return !host.empty();
}

// Reject path traversal / separators from remote FileName fields.
std::wstring SafePackageFileName(const std::wstring& name)
{
    std::wstring base = FileNameOf(name);
    std::wstring out;
    out.reserve(base.size());
    for (wchar_t c : base)
    {
        if (c == L'\\' || c == L'/' || c == L':' || c == L'\0')
            continue;
        out.push_back(c);
    }
    if (out.empty() || out == L"." || out == L"..")
        return {};
    // Require a .nupkg leaf (case-insensitive).
    if (out.size() < 6)
        return {};
    const std::wstring tail = out.substr(out.size() - 6);
    std::wstring tailLower = tail;
    for (auto& ch : tailLower)
        ch = static_cast<wchar_t>(towlower(ch));
    if (tailLower != L".nupkg")
        return {};
    return out;
}

struct HttpSession
{
    HINTERNET session = nullptr;
    HINTERNET conn = nullptr;
    HINTERNET req = nullptr;

    ~HttpSession()
    {
        if (req)
            WinHttpCloseHandle(req);
        if (conn)
            WinHttpCloseHandle(conn);
        if (session)
            WinHttpCloseHandle(session);
    }
};

// Open GET request. Requires HTTPS (update packages must not ride plain HTTP).
bool HttpOpenGet(const std::wstring& url, HttpSession& hs, std::wstring* error)
{
    std::wstring host, path;
    INTERNET_PORT port = 0;
    bool https = false;
    if (!ParseUrl(url, host, path, port, https))
    {
        if (error)
            *error = L"bad URL: " + url;
        return false;
    }
    if (!https)
    {
        if (error)
            *error = L"update feed must use HTTPS: " + url;
        return false;
    }

    hs.session = WinHttpOpen(L"QiuckPrompts-Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hs.session)
    {
        if (error)
            *error = L"WinHttpOpen failed: " + LastErrorMessage();
        return false;
    }

    hs.conn =
        WinHttpConnect(hs.session, host.c_str(), port ? port : INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hs.conn)
    {
        if (error)
            *error = L"WinHttpConnect failed: " + LastErrorMessage();
        return false;
    }

    hs.req = WinHttpOpenRequest(hs.conn, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hs.req)
    {
        if (error)
            *error = L"WinHttpOpenRequest failed: " + LastErrorMessage();
        return false;
    }

    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hs.req, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

    if (!WinHttpSendRequest(hs.req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                            0) ||
        !WinHttpReceiveResponse(hs.req, nullptr))
    {
        if (error)
            *error = L"HTTP request failed: " + LastErrorMessage();
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(hs.req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX))
    {
        if (error)
            *error = L"WinHttpQueryHeaders failed: " + LastErrorMessage();
        return false;
    }
    if (status < 200 || status >= 300)
    {
        if (error)
        {
            wchar_t buf[64];
            swprintf(buf, 64, L"HTTP status %lu", status);
            *error = buf;
        }
        return false;
    }
    return true;
}

bool HttpGetBytes(const std::wstring& url, std::string& out, std::wstring* error)
{
    out.clear();
    HttpSession hs;
    if (!HttpOpenGet(url, hs, error))
        return false;

    for (;;)
    {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hs.req, &avail))
        {
            if (error)
                *error = L"WinHttpQueryDataAvailable failed: " + LastErrorMessage();
            return false;
        }
        if (avail == 0)
            break;
        std::string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(hs.req, chunk.data(), avail, &read))
        {
            if (error)
                *error = L"WinHttpReadData failed: " + LastErrorMessage();
            return false;
        }
        if (read == 0)
            break;
        chunk.resize(read);
        out.append(chunk);
        if (out.size() > 16ull * 1024 * 1024) // JSON feeds should be tiny
        {
            if (error)
                *error = L"response too large";
            return false;
        }
    }
    return true;
}

// Stream package bytes straight to disk (avoid buffering whole nupkg in RAM).
bool HttpDownloadFile(const std::wstring& url, const std::wstring& destPath, std::wstring* error)
{
    HttpSession hs;
    if (!HttpOpenGet(url, hs, error))
        return false;

    {
        const size_t slash = destPath.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
            EnsureDirectory(destPath.substr(0, slash));
    }

    HANDLE f = CreateFileW(destPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
    {
        if (error)
            *error = L"cannot write package: " + LastErrorMessage();
        return false;
    }

    std::uint64_t total = 0;
    bool ok = true;
    for (;;)
    {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hs.req, &avail))
        {
            if (error)
                *error = L"WinHttpQueryDataAvailable failed: " + LastErrorMessage();
            ok = false;
            break;
        }
        if (avail == 0)
            break;
        std::vector<char> buf(avail);
        DWORD read = 0;
        if (!WinHttpReadData(hs.req, buf.data(), avail, &read))
        {
            if (error)
                *error = L"WinHttpReadData failed: " + LastErrorMessage();
            ok = false;
            break;
        }
        if (read == 0)
            break;
        DWORD written = 0;
        if (!WriteFile(f, buf.data(), read, &written, nullptr) || written != read)
        {
            if (error)
                *error = L"short write downloading package";
            ok = false;
            break;
        }
        total += written;
        if (total > 512ull * 1024 * 1024) // 512 MiB safety
        {
            if (error)
                *error = L"download too large";
            ok = false;
            break;
        }
    }
    CloseHandle(f);
    if (!ok)
    {
        DeleteFileW(destPath.c_str());
        return false;
    }
    if (total == 0)
    {
        DeleteFileW(destPath.c_str());
        if (error)
            *error = L"empty download";
        return false;
    }
    return true;
}

// Extract "Version":"x.y.z" and "FileName":"..." for Type Full from releases.win.json
bool ParseReleasesJson(const std::string& json, std::string& version, std::string& fileName)
{
    version.clear();
    fileName.clear();
    // Prefer a Full asset entry
    size_t pos = 0;
    while (pos < json.size())
    {
        const size_t typePos = json.find("\"Type\"", pos);
        if (typePos == std::string::npos)
            break;
        const size_t colon = json.find(':', typePos);
        if (colon == std::string::npos)
            break;
        const size_t q1 = json.find('"', colon + 1);
        if (q1 == std::string::npos)
            break;
        const size_t q2 = json.find('"', q1 + 1);
        if (q2 == std::string::npos)
            break;
        const std::string type = json.substr(q1 + 1, q2 - q1 - 1);
        if (_stricmp(type.c_str(), "Full") != 0)
        {
            pos = q2 + 1;
            continue;
        }
        // Search nearby for Version and FileName (within enclosing object — scan back/forward window)
        const size_t objStart = json.rfind('{', typePos);
        const size_t objEnd = json.find('}', typePos);
        if (objStart == std::string::npos || objEnd == std::string::npos)
        {
            pos = q2 + 1;
            continue;
        }
        const std::string obj = json.substr(objStart, objEnd - objStart + 1);
        auto getStr = [&](const char* key, std::string& out) -> bool {
            const std::string pat = std::string("\"") + key + "\"";
            size_t p = obj.find(pat);
            if (p == std::string::npos)
                return false;
            p = obj.find(':', p);
            if (p == std::string::npos)
                return false;
            p = obj.find('"', p + 1);
            if (p == std::string::npos)
                return false;
            const size_t e = obj.find('"', p + 1);
            if (e == std::string::npos)
                return false;
            out = obj.substr(p + 1, e - p - 1);
            return true;
        };
        if (getStr("Version", version) && getStr("FileName", fileName))
            return true;
        pos = objEnd + 1;
    }
    return false;
}

struct SemVer
{
    int major = 0;
    int minor = 0;
    int patch = 0;
};

bool ParseSemVer(const std::string& s, SemVer& v)
{
    v = {};
    int n = sscanf_s(s.c_str(), "%d.%d.%d", &v.major, &v.minor, &v.patch);
    return n >= 1;
}

int CmpSemVer(const SemVer& a, const SemVer& b)
{
    if (a.major != b.major)
        return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor)
        return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch)
        return a.patch < b.patch ? -1 : 1;
    return 0;
}

std::wstring JoinUrl(const std::wstring& base, const std::wstring& file)
{
    if (base.empty())
        return file;
    if (!base.empty() && (base.back() == L'/' || base.back() == L'\\'))
        return base + file;
    return base + L"/" + file;
}

std::string TrimAscii(std::string s)
{
    while (!s.empty() &&
           (s.back() == ' ' || s.back() == '\r' || s.back() == '\n' || s.back() == '\t'))
        s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\r' || s[i] == '\n' || s[i] == '\t'))
        ++i;
    return s.substr(i);
}

} // namespace

std::wstring DefaultUpdateFeedUrl()
{
    return L"https://github.com/summeroff/qiuckprompts/releases/latest/download";
}

bool IsVelopackInstalled()
{
    return !FindUpdateExe().empty();
}

std::wstring FindUpdateExe()
{
    const std::wstring root = GuessInstallRoot();
    if (root.empty())
        return {};
    const std::wstring p = PathJoin(root, L"Update.exe");
    return FileExists(p) ? p : std::wstring{};
}

bool CheckForUpdates(const std::wstring& feedUrl, UpdateCheckResult& out, std::wstring* error)
{
    out = {};
    out.currentVersion = std::to_wstring(QP_VERSION_MAJOR) + L"." +
                         std::to_wstring(QP_VERSION_MINOR) + L"." +
                         std::to_wstring(QP_VERSION_PATCH);

    const std::wstring updateExe = FindUpdateExe();
    out.installed = !updateExe.empty();
    if (!out.installed)
    {
        out.ok = true;
        out.detail = L"Not a Velopack install (no Update.exe). Use Setup.exe from GitHub Releases.";
        QP_LOG_INFO(L"updater: not installed via Velopack");
        return true;
    }

    std::wstring feed = feedUrl.empty() ? DefaultUpdateFeedUrl() : feedUrl;
    // strip trailing slash for join
    while (!feed.empty() && (feed.back() == L'/' || feed.back() == L'\\'))
        feed.pop_back();

    const std::wstring jsonUrl = JoinUrl(feed, L"releases.win.json");
    QP_LOG_INFO(L"updater: fetching %s", jsonUrl.c_str());

    std::string body;
    std::wstring err;
    if (!HttpGetBytes(jsonUrl, body, &err))
    {
        out.ok = false;
        out.detail = err;
        if (error)
            *error = err;
        return false;
    }

    std::string ver, file;
    if (!ParseReleasesJson(body, ver, file))
    {
        out.ok = false;
        out.detail = L"could not parse releases.win.json";
        if (error)
            *error = out.detail;
        return false;
    }
    ver = TrimAscii(ver);
    file = TrimAscii(file);

    out.remoteVersion = Utf8ToWide(ver);
    out.packageFileName = SafePackageFileName(Utf8ToWide(file));
    if (out.packageFileName.empty())
    {
        out.ok = false;
        out.detail = L"invalid package FileName in releases.win.json";
        if (error)
            *error = out.detail;
        return false;
    }
    // URL uses the remote basename only (already sanitized) under the feed directory.
    out.packageUrl = JoinUrl(feed, out.packageFileName);
    out.ok = true;

    SemVer cur{}, remote{};
    ParseSemVer(WideToUtf8(out.currentVersion), cur);
    ParseSemVer(ver, remote);
    out.updateAvailable = CmpSemVer(remote, cur) > 0;

    if (out.updateAvailable)
    {
        out.detail = L"Update available: " + out.currentVersion + L" → " + out.remoteVersion;
    } else
    {
        out.detail = L"Up to date (" + out.currentVersion + L"; remote " + out.remoteVersion + L")";
    }
    QP_LOG_INFO(L"updater: %s", out.detail.c_str());
    return true;
}

bool DownloadAndApplyUpdate(const UpdateCheckResult& check, std::wstring* error)
{
    if (!check.ok || !check.updateAvailable)
    {
        if (error)
            *error = L"no update to apply";
        return false;
    }
    const std::wstring updateExe = FindUpdateExe();
    if (updateExe.empty())
    {
        if (error)
            *error = L"Update.exe not found";
        return false;
    }

    const std::wstring tmpDir = PathJoin(GetAppDataDir(true), L"updates");
    EnsureDirectory(tmpDir);
    const std::wstring nupkgPath = PathJoin(tmpDir, check.packageFileName);

    QP_LOG_INFO(L"updater: downloading %s → %s", check.packageUrl.c_str(), nupkgPath.c_str());
    std::wstring err;
    if (!HttpDownloadFile(check.packageUrl, nupkgPath, &err))
    {
        if (error)
            *error = err;
        return false;
    }

    // Update.exe apply --package <file> --waitPid <pid>
    // Default restarts the app after apply.
    const DWORD pid = GetCurrentProcessId();
    std::wstring args = L"apply --package \"";
    args += nupkgPath;
    args += L"\" --waitPid ";
    args += std::to_wstring(pid);

    QP_LOG_INFO(L"updater: launching %s %s", updateExe.c_str(), args.c_str());

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"open";
    sei.lpFile = updateExe.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_SHOW;
    if (!ShellExecuteExW(&sei))
    {
        if (error)
            *error = L"failed to start Update.exe: " + LastErrorMessage();
        return false;
    }
    if (sei.hProcess)
        CloseHandle(sei.hProcess);

    // Apply waits for us to exit — quit the message loop.
    return true;
}

void RunUpdateFlowInteractive(const std::wstring& feedUrl, HWND owner)
{
    UpdateCheckResult check;
    std::wstring err;
    if (!CheckForUpdates(feedUrl, check, &err))
    {
        MessageBoxW(owner, (L"Update check failed:\n" + err).c_str(), QP_APP_DISPLAY_W,
                    MB_OK | MB_ICONWARNING);
        return;
    }

    if (!check.installed)
    {
        MessageBoxW(owner,
                    (check.detail +
                     L"\n\nInstall via QiuckPrompts-win-Setup.exe from GitHub Releases "
                     L"to enable in-app updates.")
                        .c_str(),
                    QP_APP_DISPLAY_W, MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (!check.updateAvailable)
    {
        MessageBoxW(owner, check.detail.c_str(), QP_APP_DISPLAY_W, MB_OK | MB_ICONINFORMATION);
        return;
    }

    const int choice = MessageBoxW(
        owner, (check.detail + L"\n\nDownload and install now? The app will restart.").c_str(),
        QP_APP_DISPLAY_W, MB_YESNO | MB_ICONQUESTION);
    if (choice != IDYES)
        return;

    if (!DownloadAndApplyUpdate(check, &err))
    {
        MessageBoxW(owner, (L"Update failed:\n" + err).c_str(), QP_APP_DISPLAY_W,
                    MB_OK | MB_ICONERROR);
        return;
    }

    // Update.exe is waiting for this PID — exit cleanly.
    QP_LOG_INFO(L"updater: exiting so Update.exe can apply");
    if (owner)
        PostMessageW(owner, WM_CLOSE, 0, 0);
    else
        PostQuitMessage(0);
}

namespace
{

bool CopyDirectoryRecursive(const std::wstring& srcDir, const std::wstring& dstDir)
{
    if (!DirectoryExists(srcDir))
        return false;
    if (!EnsureDirectory(dstDir))
        return false;

    const std::wstring pattern = PathJoin(srcDir, L"*");
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    bool ok = true;
    do
    {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        const std::wstring from = PathJoin(srcDir, fd.cFileName);
        const std::wstring to = PathJoin(dstDir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (!CopyDirectoryRecursive(from, to))
            {
                ok = false;
                break;
            }
        } else
        {
            if (!CopyFileW(from.c_str(), to.c_str(), FALSE))
            {
                ok = false;
                break;
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return ok;
}

const wchar_t* MatchVeloHook(const std::wstring& arg)
{
    static const wchar_t* kHooks[] = {
        L"--veloapp-install",
        L"--veloapp-updated",
        L"--veloapp-obsolete",
        L"--veloapp-uninstall",
    };
    for (const wchar_t* h : kHooks)
    {
        if (arg == h)
            return h;
        const size_t n = wcslen(h);
        if (arg.size() > n && arg.compare(0, n, h) == 0 && arg[n] == L'=')
            return h;
    }
    return nullptr;
}

void InitHookLog()
{
    const std::wstring logDir = GetUserLogsDir(true);
    const std::wstring logFile = PathJoin(logDir, L"velopack-hook.log");
    Logger::Instance().Init(logFile, LogLevel::Info, false);
}

} // namespace

std::wstring GetStableExtensionDir(bool ensure)
{
    const std::wstring dir = PathJoin(GetAppDataDir(ensure), L"extension");
    if (ensure)
        EnsureDirectory(dir);
    return dir;
}

bool SyncPackagedExtensionToStable(std::wstring* error)
{
    const std::wstring src = PathJoin(GetExeDir(), L"extension");
    if (!DirectoryExists(src))
    {
        if (error)
            *error = L"packaged extension/ missing next to exe";
        return false;
    }
    const std::wstring dst = GetStableExtensionDir(true);
    if (!CopyDirectoryRecursive(src, dst))
    {
        if (error)
            *error = L"failed to copy extension to " + dst;
        return false;
    }
    QP_LOG_INFO(L"updater: synced extension %s → %s", src.c_str(), dst.c_str());
    return true;
}

bool TryHandleVelopackHook(int argc, wchar_t** argv, int* exitCode)
{
    if (exitCode)
        *exitCode = 0;
    if (!argv || argc < 2)
        return false;

    const wchar_t* hook = nullptr;
    std::wstring version;
    for (int i = 1; i < argc; ++i)
    {
        if (!argv[i])
            continue;
        const std::wstring arg = argv[i];
        const wchar_t* h = MatchVeloHook(arg);
        if (!h)
            continue;
        hook = h;
        const size_t n = wcslen(h);
        if (arg.size() > n && arg[n] == L'=')
            version = arg.substr(n + 1);
        else if (i + 1 < argc && argv[i + 1] && argv[i + 1][0] != L'-')
            version = argv[i + 1];
        break;
    }
    if (!hook)
        return false;

    InitHookLog();
    QP_LOG_INFO(L"velopack hook %s version=%s", hook, version.c_str());

    int code = 0;
    try
    {
        if (wcscmp(hook, L"--veloapp-uninstall") == 0)
        {
            std::wstring nmErr;
            if (!RemoveNativeMessagingRegistration(&nmErr))
                QP_LOG_WARN(L"velopack uninstall: NM cleanup: %s", nmErr.c_str());
            else
                QP_LOG_INFO(L"velopack uninstall: NM registry cleared");

            std::wstring autoErr;
            if (!SyncStartWithWindows(false, &autoErr))
                QP_LOG_WARN(L"velopack uninstall: autostart cleanup failed: %s", autoErr.c_str());
            else
                QP_LOG_INFO(L"velopack uninstall: autostart Run key cleared");
        } else if (wcscmp(hook, L"--veloapp-obsolete") == 0)
        {
            QP_LOG_INFO(L"velopack obsolete: no-op");
        } else
        {
            // install + updated
            std::wstring cfgPath;
            std::wstring err;
            if (!EnsureUserConfigFile(&cfgPath, &err))
                QP_LOG_WARN(L"velopack hook: EnsureUserConfigFile: %s", err.c_str());
            else
                QP_LOG_INFO(L"velopack hook: user config %s", cfgPath.c_str());

            err.clear();
            if (!SyncPackagedExtensionToStable(&err))
                QP_LOG_WARN(L"velopack hook: extension sync: %s", err.c_str());

            err.clear();
            if (!EnsureNativeMessagingRegistration(L"", &err))
            {
                QP_LOG_ERROR(L"velopack hook: NM register failed: %s", err.c_str());
                code = 1;
            } else
            {
                QP_LOG_INFO(L"velopack hook: NM host registered");
            }
        }
    } catch (...)
    {
        QP_LOG_ERROR(L"velopack hook: unexpected exception");
        code = 1;
    }

    Logger::Instance().Shutdown();
    if (exitCode)
        *exitCode = code;
    return true;
}

} // namespace qp
