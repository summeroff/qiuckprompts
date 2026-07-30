#include "ext_bridge.hpp"
#include "logger.hpp"
#include "util.hpp"
#include "version.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

namespace qp
{

namespace
{

constexpr DWORD kPipeBuffer = 1 << 20; // 1 MiB — prompts can be large
constexpr DWORD kConnectWaitMs = 500;

bool ReadExact(HANDLE h, void* buf, DWORD n, DWORD timeoutMs)
{
    BYTE* p = static_cast<BYTE*>(buf);
    DWORD gotTotal = 0;
    const DWORD start = GetTickCount();
    while (gotTotal < n)
    {
        if (timeoutMs != INFINITE)
        {
            const DWORD elapsed = GetTickCount() - start;
            if (elapsed >= timeoutMs)
                return false;
        }
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr))
        {
            // stdin may not support Peek — fall through to ReadFile
            avail = n - gotTotal;
        }
        if (avail == 0)
        {
            Sleep(5);
            continue;
        }
        DWORD got = 0;
        const DWORD want = (std::min)(avail, n - gotTotal);
        if (!ReadFile(h, p + gotTotal, want, &got, nullptr) || got == 0)
            return false;
        gotTotal += got;
    }
    return true;
}

bool WriteExact(HANDLE h, const void* buf, DWORD n)
{
    const BYTE* p = static_cast<const BYTE*>(buf);
    DWORD sent = 0;
    while (sent < n)
    {
        DWORD w = 0;
        if (!WriteFile(h, p + sent, n - sent, &w, nullptr) || w == 0)
            return false;
        sent += w;
    }
    return true;
}

// Length-prefixed frame (Chrome native messaging): uint32 LE + utf8 payload.
bool ReadNmFrame(HANDLE h, std::string& out, DWORD timeoutMs)
{
    std::uint32_t len = 0;
    if (!ReadExact(h, &len, 4, timeoutMs))
        return false;
    if (len == 0 || len > kPipeBuffer)
        return false;
    out.assign(len, '\0');
    return ReadExact(h, out.data(), len, timeoutMs);
}

bool WriteNmFrame(HANDLE h, const std::string& utf8)
{
    if (utf8.size() > kPipeBuffer)
        return false;
    const std::uint32_t len = static_cast<std::uint32_t>(utf8.size());
    if (!WriteExact(h, &len, 4))
        return false;
    return WriteExact(h, utf8.data(), len);
}

std::wstring JsonToWidePathEscape(const std::wstring& path)
{
    // JSON string with backslashes doubled.
    std::wstring out;
    out.reserve(path.size() + 8);
    for (wchar_t c : path)
    {
        if (c == L'\\' || c == L'"')
            out.push_back(L'\\');
        out.push_back(c);
    }
    return out;
}

bool WriteTextFileUtf8(const std::wstring& path, const std::string& utf8)
{
    {
        const size_t slash = path.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
            EnsureDirectory(path.substr(0, slash));
    }
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD w = 0;
    const BOOL ok = WriteFile(f, utf8.data(), static_cast<DWORD>(utf8.size()), &w, nullptr);
    CloseHandle(f);
    return ok == TRUE;
}

bool SetNmRegistry(const wchar_t* subKey, const std::wstring& manifestPath)
{
    HKEY key = nullptr;
    const LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, subKey, 0, nullptr, 0, KEY_SET_VALUE,
                                    nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS)
        return false;
    const LONG rc2 =
        RegSetValueExW(key, nullptr, 0, REG_SZ, reinterpret_cast<const BYTE*>(manifestPath.c_str()),
                       static_cast<DWORD>((manifestPath.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return rc2 == ERROR_SUCCESS;
}

// Skip whitespace in json snippet.
size_t SkipWs(const std::string& s, size_t i)
{
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
        ++i;
    return i;
}

} // namespace

// ---- tiny JSON helpers ----------------------------------------------------

std::string JsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20)
            {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else
            {
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    return out;
}

bool JsonGetString(const std::string& json, const char* key, std::string& out)
{
    out.clear();
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos)
        return false;
    p = json.find(':', p + pat.size());
    if (p == std::string::npos)
        return false;
    p = SkipWs(json, p + 1);
    if (p >= json.size() || json[p] != '"')
        return false;
    ++p;
    std::string acc;
    while (p < json.size())
    {
        const char c = json[p++];
        if (c == '"')
        {
            out = acc;
            return true;
        }
        if (c == '\\' && p < json.size())
        {
            const char e = json[p++];
            switch (e)
            {
            case '"':
            case '\\':
            case '/':
                acc.push_back(e);
                break;
            case 'n':
                acc.push_back('\n');
                break;
            case 'r':
                acc.push_back('\r');
                break;
            case 't':
                acc.push_back('\t');
                break;
            case 'u':
                // skip 4 hex — insert '?'
                if (p + 4 <= json.size())
                    p += 4;
                acc.push_back('?');
                break;
            default:
                acc.push_back(e);
                break;
            }
        } else
        {
            acc.push_back(c);
        }
    }
    return false;
}

bool JsonGetBool(const std::string& json, const char* key, bool& out)
{
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos)
        return false;
    p = json.find(':', p + pat.size());
    if (p == std::string::npos)
        return false;
    p = SkipWs(json, p + 1);
    if (json.compare(p, 4, "true") == 0)
    {
        out = true;
        return true;
    }
    if (json.compare(p, 5, "false") == 0)
    {
        out = false;
        return true;
    }
    return false;
}

bool JsonGetInt64(const std::string& json, const char* key, std::int64_t& out)
{
    const std::string pat = std::string("\"") + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos)
        return false;
    p = json.find(':', p + pat.size());
    if (p == std::string::npos)
        return false;
    p = SkipWs(json, p + 1);
    char* end = nullptr;
    const long long v = _strtoi64(json.c_str() + p, &end, 10);
    if (end == json.c_str() + p)
        return false;
    out = static_cast<std::int64_t>(v);
    return true;
}

// ---- ExtBridge ------------------------------------------------------------

ExtBridge& ExtBridge::Instance()
{
    static ExtBridge g;
    return g;
}

ExtBridge::~ExtBridge()
{
    Stop();
}

bool ExtBridge::Start(std::wstring* error)
{
    if (running_.load())
        return true;

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_)
    {
        if (error)
            *error = L"CreateEvent failed: " + LastErrorMessage();
        return false;
    }

    running_ = true;
    thread_ = CreateThread(nullptr, 0, ServerThreadMain, this, 0, nullptr);
    if (!thread_)
    {
        running_ = false;
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
        if (error)
            *error = L"CreateThread failed: " + LastErrorMessage();
        return false;
    }
    QP_LOG_INFO(L"ext_bridge: pipe server starting (%s)", QP_EXT_BRIDGE_PIPE_W);
    return true;
}

void ExtBridge::Stop()
{
    if (!running_.exchange(false))
    {
        return;
    }
    if (stopEvent_)
        SetEvent(stopEvent_);

    // Unblock ConnectNamedPipe / ReadFile by opening a dummy client.
    HANDLE dummy = CreateFileW(QP_EXT_BRIDGE_PIPE_W, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, 0, nullptr);
    if (dummy != INVALID_HANDLE_VALUE)
        CloseHandle(dummy);

    {
        std::lock_guard<std::mutex> lock(ioMutex_);
        if (pipe_ != INVALID_HANDLE_VALUE)
        {
            CancelIoEx(pipe_, nullptr);
            CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
        }
    }

    if (thread_)
    {
        WaitForSingleObject(thread_, 5000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (stopEvent_)
    {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    clientConnected_ = false;
    QP_LOG_INFO(L"ext_bridge: stopped");
}

DWORD WINAPI ExtBridge::ServerThreadMain(void* self)
{
    static_cast<ExtBridge*>(self)->ServerLoop();
    return 0;
}

void ExtBridge::ServerLoop()
{
    while (running_.load())
    {
        HANDLE pipe = CreateNamedPipeW(QP_EXT_BRIDGE_PIPE_W, PIPE_ACCESS_DUPLEX,
                                       PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1,
                                       kPipeBuffer, kPipeBuffer, kConnectWaitMs, nullptr);
        if (pipe == INVALID_HANDLE_VALUE)
        {
            QP_LOG_ERROR(L"ext_bridge: CreateNamedPipe failed: %s", LastErrorMessage().c_str());
            Sleep(500);
            continue;
        }

        const BOOL connected =
            ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (!running_.load())
        {
            CloseHandle(pipe);
            break;
        }

        if (!connected)
        {
            CloseHandle(pipe);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(ioMutex_);
            pipe_ = pipe;
        }
        clientConnected_ = true;
        QP_LOG_INFO(L"ext_bridge: native-host client connected");

        // Read loop until disconnect
        for (;;)
        {
            if (!running_.load())
                break;
            std::string frame;
            // Long timeout — host is idle most of the time; stopEvent checked via running_
            if (!ReadFrame(pipe, frame, 2000))
            {
                if (!running_.load())
                    break;
                // Distinguish timeout (stay) vs disconnect
                DWORD avail = 0;
                if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr))
                {
                    QP_LOG_INFO(L"ext_bridge: client disconnected");
                    break;
                }
                continue;
            }
            HandleIncoming(frame);
        }

        clientConnected_ = false;
        {
            std::lock_guard<std::mutex> lock(ioMutex_);
            if (pipe_ == pipe)
                pipe_ = INVALID_HANDLE_VALUE;
        }
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

bool ExtBridge::WriteFrame(HANDLE pipe, const std::string& utf8)
{
    return WriteNmFrame(pipe, utf8);
}

bool ExtBridge::ReadFrame(HANDLE pipe, std::string& utf8, DWORD timeoutMs)
{
    return ReadNmFrame(pipe, utf8, timeoutMs);
}

void ExtBridge::HandleIncoming(const std::string& utf8)
{
    std::int64_t id = 0;
    if (!JsonGetInt64(utf8, "id", id))
    {
        QP_LOG_DEBUG(L"ext_bridge: frame without id ignored (%zu bytes)", utf8.size());
        return;
    }
    {
        std::lock_guard<std::mutex> lock(waitMutex_);
        pending_[id] = utf8;
    }
    waitCv_.notify_all();
}

bool ExtBridge::IsClientConnected() const
{
    return clientConnected_.load();
}

bool ExtBridge::IsExtensionReady() const
{
    return clientConnected_.load();
}

bool ExtBridge::Call(const std::string& requestJson, std::string& responseJson, DWORD timeoutMs,
                     std::wstring* error)
{
    responseJson.clear();
    if (!clientConnected_.load())
    {
        if (error)
            *error = L"extension host not connected (load MV3 ext + allow native messaging)";
        return false;
    }

    std::string req = requestJson;
    std::int64_t id = 0;
    if (!JsonGetInt64(req, "id", id))
    {
        id = nextId_.fetch_add(1);
        // inject "id":N after first {
        const size_t brace = req.find('{');
        if (brace == std::string::npos)
        {
            if (error)
                *error = L"invalid request json";
            return false;
        }
        char idbuf[32];
        snprintf(idbuf, sizeof(idbuf), "\"id\":%lld,", static_cast<long long>(id));
        req.insert(brace + 1, idbuf);
    }

    {
        std::lock_guard<std::mutex> lock(waitMutex_);
        pending_.erase(id);
    }

    {
        std::lock_guard<std::mutex> lock(ioMutex_);
        if (pipe_ == INVALID_HANDLE_VALUE || !WriteFrame(pipe_, req))
        {
            if (error)
                *error = L"failed to write to extension host pipe";
            return false;
        }
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    std::unique_lock<std::mutex> lock(waitMutex_);
    while (pending_.find(id) == pending_.end())
    {
        if (waitCv_.wait_until(lock, deadline) == std::cv_status::timeout)
        {
            if (error)
                *error = L"extension call timed out";
            return false;
        }
    }
    responseJson = std::move(pending_[id]);
    pending_.erase(id);
    return true;
}

bool ExtBridge::Ping(DWORD timeoutMs)
{
    std::string resp;
    std::wstring err;
    if (!Call("{\"cmd\":\"ping\"}", resp, timeoutMs, &err))
        return false;
    bool ok = false;
    return JsonGetBool(resp, "ok", ok) && ok;
}

bool ExtBridge::PrepareAndPaste(const std::wstring& url, const std::wstring& text, DWORD timeoutMs,
                                std::wstring* detail, std::wstring* error)
{
    const std::string urlU = WideToUtf8(url);
    const std::string textU = WideToUtf8(text);
    std::ostringstream oss;
    oss << "{\"cmd\":\"prepareAndPaste\",\"url\":\"" << JsonEscape(urlU) << "\",\"text\":\""
        << JsonEscape(textU) << "\",\"timeoutMs\":" << static_cast<unsigned long>(timeoutMs) << "}";

    std::string resp;
    if (!Call(oss.str(), resp, timeoutMs + 2000, error))
        return false;

    bool ok = false;
    JsonGetBool(resp, "ok", ok);
    std::string d;
    if (JsonGetString(resp, "detail", d) || JsonGetString(resp, "error", d))
    {
        if (detail)
            *detail = Utf8ToWide(d);
    }
    if (!ok && error && error->empty())
    {
        *error = detail && !detail->empty() ? *detail : L"extension prepareAndPaste failed";
    }
    return ok;
}

bool EnsureNativeMessagingRegistration(const std::wstring& extensionId, std::wstring* error)
{
    const std::wstring extId = extensionId.empty() ? QP_EXTENSION_ID_W : extensionId;
    const std::wstring nmDir = PathJoin(GetAppDataDir(true), L"nm");
    EnsureDirectory(nmDir);

    const std::wstring batPath = PathJoin(nmDir, L"com.qiuckprompts.host.bat");
    const std::wstring manifestPath = PathJoin(nmDir, L"com.qiuckprompts.host.json");
    const std::wstring exePath = GetExePath();

    // Batch wrapper: Chrome NM path cannot take argv on Windows.
    {
        std::string bat = "@echo off\r\n\"";
        bat += WideToUtf8(exePath);
        bat += "\" --native-messaging-host\r\n";
        if (!WriteTextFileUtf8(batPath, bat))
        {
            if (error)
                *error = L"failed to write NM host bat";
            return false;
        }
    }

    {
        // JSON path must use escaped backslashes.
        const std::string pathEsc = WideToUtf8(JsonToWidePathEscape(batPath));
        const std::string origin = std::string("chrome-extension://") + WideToUtf8(extId) + "/";
        std::ostringstream oss;
        oss << "{\n"
            << "  \"name\": \"" << QP_NM_HOST_NAME << "\",\n"
            << "  \"description\": \"QiuckPrompts native messaging host\",\n"
            << "  \"path\": \"" << pathEsc << "\",\n"
            << "  \"type\": \"stdio\",\n"
            << "  \"allowed_origins\": [\n"
            << "    \"" << origin << "\"\n"
            << "  ]\n"
            << "}\n";
        if (!WriteTextFileUtf8(manifestPath, oss.str()))
        {
            if (error)
                *error = L"failed to write NM host manifest";
            return false;
        }
    }

    const std::wstring valueName = QP_NM_HOST_NAME_W;
    // Registry: key path ends with host name; default value = manifest path.
    const std::wstring chromeKey =
        std::wstring(L"Software\\Google\\Chrome\\NativeMessagingHosts\\") + valueName;
    const std::wstring chromiumKey =
        std::wstring(L"Software\\Chromium\\NativeMessagingHosts\\") + valueName;
    const std::wstring edgeKey =
        std::wstring(L"Software\\Microsoft\\Edge\\NativeMessagingHosts\\") + valueName;

    bool any = false;
    any = SetNmRegistry(chromeKey.c_str(), manifestPath) || any;
    any = SetNmRegistry(chromiumKey.c_str(), manifestPath) || any;
    any = SetNmRegistry(edgeKey.c_str(), manifestPath) || any;

    if (!any)
    {
        if (error)
            *error = L"failed to write NM host registry keys";
        return false;
    }

    QP_LOG_INFO(L"ext_bridge: NM host registered ext=%s manifest=%s", extId.c_str(),
                manifestPath.c_str());
    return true;
}

int RunNativeMessagingHost()
{
    // Connect to tray pipe (retry a few seconds — tray may still be starting).
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int i = 0; i < 50; ++i)
    {
        pipe = CreateFileW(QP_EXT_BRIDGE_PIPE_W, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE)
            break;
        Sleep(100);
    }
    if (pipe == INVALID_HANDLE_VALUE)
    {
        // Still answer Chrome so the extension sees a clean error path.
        const std::string err =
            "{\"ok\":false,\"error\":\"tray not running — start QiuckPrompts first\"}";
        WriteNmFrame(GetStdHandle(STD_OUTPUT_HANDLE), err);
        return 1;
    }

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    // Two directions: Chrome stdin -> pipe, pipe -> Chrome stdout.
    // Single-threaded alternate with Peek to avoid needing extra threads for MVP.
    // Prefer two threads for blocking reads.

    struct Ctx
    {
        HANDLE pipe;
        HANDLE hIn;
        HANDLE hOut;
        std::atomic<bool> alive{true};
    } ctx{pipe, hIn, hOut, true};

    HANDLE t1 = CreateThread(
        nullptr, 0,
        [](void* p) -> DWORD {
            auto* c = static_cast<Ctx*>(p);
            while (c->alive.load())
            {
                std::string frame;
                if (!ReadNmFrame(c->hIn, frame, INFINITE))
                    break;
                if (!WriteNmFrame(c->pipe, frame))
                    break;
            }
            c->alive = false;
            // Wake peer
            CancelIoEx(c->pipe, nullptr);
            return 0;
        },
        &ctx, 0, nullptr);

    HANDLE t2 = CreateThread(
        nullptr, 0,
        [](void* p) -> DWORD {
            auto* c = static_cast<Ctx*>(p);
            while (c->alive.load())
            {
                std::string frame;
                if (!ReadNmFrame(c->pipe, frame, 2000))
                {
                    if (!c->alive.load())
                        break;
                    DWORD avail = 0;
                    if (!PeekNamedPipe(c->pipe, nullptr, 0, nullptr, &avail, nullptr))
                        break;
                    continue;
                }
                if (!WriteNmFrame(c->hOut, frame))
                    break;
            }
            c->alive = false;
            CancelIoEx(c->hIn, nullptr);
            return 0;
        },
        &ctx, 0, nullptr);

    if (t1)
        WaitForSingleObject(t1, INFINITE);
    if (t2)
        WaitForSingleObject(t2, INFINITE);
    if (t1)
        CloseHandle(t1);
    if (t2)
        CloseHandle(t2);
    CloseHandle(pipe);
    return 0;
}

} // namespace qp
