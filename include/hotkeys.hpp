#pragma once

#include <windows.h>

#include "config.hpp"

#include <functional>
#include <vector>

namespace qp
{

// Global hotkeys via RegisterHotKey.
//
// Default trigger mode is OnRelease:
//   WM_HOTKEY arms the binding → poll until chord keys are up → then callback.
// That way Ctrl/Alt are no longer held when the workflow starts (select-all/copy/paste).
class HotkeyManager
{
public:
    using Callback = std::function<void(int id, const HotkeyBinding& binding)>;

    static constexpr UINT_PTR kReleaseTimerId = 1;

    HotkeyManager() = default;
    ~HotkeyManager();

    HotkeyManager(const HotkeyManager&) = delete;
    HotkeyManager& operator=(const HotkeyManager&) = delete;

    void SetCallback(Callback cb) { callback_ = std::move(cb); }

    // Assigns binding.id = 1..N and registers. Partial success OK.
    bool RegisterAll(HWND hwnd, std::vector<HotkeyBinding> bindings, std::wstring* error = nullptr);

    void UnregisterAll();

    // Call from WndProc on WM_HOTKEY.
    void OnWmHotkey(WPARAM wParam);

    // Call from WndProc on WM_TIMER (timer id == kReleaseTimerId).
    void OnTimer(WPARAM timerId);

    // While true, armed/fired hotkeys are ignored (long-running workflow).
    void SetBusy(bool busy);
    bool Busy() const { return busy_; }

    void SetTriggerMode(HotkeyTriggerMode mode) { triggerMode_ = mode; }
    HotkeyTriggerMode TriggerMode() const { return triggerMode_; }

    void SetReleaseTimeoutMs(int ms) { releaseTimeoutMs_ = ms; }
    void SetReleasePollMs(int ms) { releasePollMs_ = ms; }

    const HotkeyBinding* FindById(int id) const;

    size_t RegisteredCount() const { return registered_.size(); }
    const std::vector<HotkeyBinding>& Bindings() const { return registered_; }

private:
    void ArmPending(int id);
    void ClearPending(const wchar_t* reason);
    void FirePending(const wchar_t* reason);
    bool IsChordReleased(const HotkeyBinding& b) const;
    void StartReleaseTimer();
    void StopReleaseTimer();

    HWND hwnd_ = nullptr;
    std::vector<HotkeyBinding> registered_;
    Callback callback_;

    HotkeyTriggerMode triggerMode_ = HotkeyTriggerMode::OnRelease;
    int releaseTimeoutMs_ = 3000;
    int releasePollMs_ = 15;

    int pendingId_ = 0;
    DWORD pendingSince_ = 0;
    bool busy_ = false;
};

} // namespace qp
