#include "updater.hpp"
#include "logger.hpp"
#include "util.hpp"
#include "version.hpp"

#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>

#include <cstdio>
#include <cstring>
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

bool HttpGetBytes(const std::wstring& url, std::string& out, std::wstring* error)
{
    out.clear();
    std::wstring host, path;
    INTERNET_PORT port = 0;
    bool https = false;
    if (!ParseUrl(url, host, path, port, https))
    {
        if (error)
            *error = L"bad URL: " + url;
        return false;
    }

    HINTERNET session = WinHttpOpen(L"QiuckPrompts-Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
    {
        if (error)
            *error = L"WinHttpOpen failed: " + LastErrorMessage();
        return false;
    }

    HINTERNET conn = WinHttpConnect(
        session, host.c_str(),
        port ? port : (https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT), 0);
    if (!conn)
    {
        if (error)
            *error = L"WinHttpConnect failed: " + LastErrorMessage();
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req)
    {
        if (error)
            *error = L"WinHttpOpenRequest failed: " + LastErrorMessage();
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return false;
    }

    // Follow redirects (GitHub latest/download → tag asset)
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));

    BOOL ok =
        WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok)
        ok = WinHttpReceiveResponse(req, nullptr);

    if (!ok)
    {
        if (error)
            *error = L"HTTP request failed: " + LastErrorMessage();
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    if (status < 200 || status >= 300)
    {
        if (error)
        {
            wchar_t buf[64];
            swprintf(buf, 64, L"HTTP status %lu", status);
            *error = buf;
        }
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
        WinHttpCloseHandle(session);
        return false;
    }

    for (;;)
    {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail))
            break;
        if (avail == 0)
            break;
        std::string chunk(avail, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req, chunk.data(), avail, &read) || read == 0)
            break;
        chunk.resize(read);
        out.append(chunk);
        if (out.size() > 256ull * 1024 * 1024) // 256 MiB safety
        {
            if (error)
                *error = L"download too large";
            WinHttpCloseHandle(req);
            WinHttpCloseHandle(conn);
            WinHttpCloseHandle(session);
            return false;
        }
    }

    WinHttpCloseHandle(req);
    WinHttpCloseHandle(conn);
    WinHttpCloseHandle(session);
    return true;
}

bool HttpDownloadFile(const std::wstring& url, const std::wstring& destPath, std::wstring* error)
{
    std::string bytes;
    if (!HttpGetBytes(url, bytes, error))
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
    DWORD written = 0;
    const BOOL ok = WriteFile(f, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(f);
    if (!ok || written != bytes.size())
    {
        if (error)
            *error = L"short write downloading package";
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
    out.packageFileName = Utf8ToWide(file);
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

} // namespace qp
