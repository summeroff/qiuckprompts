#pragma once

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace qp
{

// Named-pipe server inside the tray process. The Chrome native-messaging host
// (--native-messaging-host) connects as a client and relays JSON to the MV3
// extension. Workflow calls Call() to prepare/paste via the extension.
class ExtBridge
{
public:
    static ExtBridge& Instance();

    bool Start(std::wstring* error = nullptr);
    void Stop();

    bool IsClientConnected() const;
    // True while a native-messaging host client holds the named-pipe connection.
    // (Does not require a successful ping — connection alone is enough to attempt Call.)
    bool IsExtensionReady() const;

    // Blocking request/response. requestJson must include "id" (number) or one is injected.
    // Returns false on timeout / no client / transport error.
    bool Call(const std::string& requestJson, std::string& responseJson, DWORD timeoutMs,
              std::wstring* error = nullptr);

    // Convenience: prepare tab + paste text via extension.
    // cancelOnFocusSwitch: when true (default), extension aborts if tab/window loses focus.
    bool PrepareAndPaste(const std::wstring& url, const std::wstring& text, DWORD timeoutMs,
                         std::wstring* detail, std::wstring* error = nullptr,
                         bool cancelOnFocusSwitch = true);

    bool Ping(DWORD timeoutMs = 1500);

private:
    ExtBridge() = default;
    ~ExtBridge();
    ExtBridge(const ExtBridge&) = delete;
    ExtBridge& operator=(const ExtBridge&) = delete;

    static DWORD WINAPI ServerThreadMain(void* self);
    void ServerLoop();
    bool WriteFrame(HANDLE pipe, const std::string& utf8);
    bool ReadFrame(HANDLE pipe, std::string& utf8, DWORD timeoutMs);
    void HandleIncoming(const std::string& utf8);

    HANDLE stopEvent_ = nullptr;
    HANDLE thread_ = nullptr;
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    mutable std::mutex ioMutex_; // serialize write to client pipe
    std::atomic<bool> clientConnected_{false};
    std::atomic<bool> running_{false};

    std::mutex waitMutex_;
    std::condition_variable waitCv_;
    std::unordered_map<std::int64_t, std::string> pending_;
    std::atomic<std::int64_t> nextId_{1};
};

// Write NM host manifest + HKCU registry for Chrome/Chromium/Edge. Safe to call often.
bool EnsureNativeMessagingRegistration(const std::wstring& extensionId,
                                       std::wstring* error = nullptr);

// Remove HKCU NM host keys (Velopack uninstall hook). Leaves AppData files.
bool RemoveNativeMessagingRegistration(std::wstring* error = nullptr);

// stdio length-prefixed JSON relay to ExtBridge pipe. Does not return until Chrome closes stdin.
int RunNativeMessagingHost();

// Tiny helpers for the simple JSON we exchange (no full parser).
std::string JsonEscape(const std::string& s);
bool JsonGetString(const std::string& json, const char* key, std::string& out);
bool JsonGetBool(const std::string& json, const char* key, bool& out);
bool JsonGetInt64(const std::string& json, const char* key, std::int64_t& out);

} // namespace qp
