#include "hotkeys.hpp"
#include "logger.hpp"
#include "util.hpp"

namespace qp
{

namespace
{

bool IsVkDown(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

bool AnyControlDown()
{
    return IsVkDown(VK_CONTROL) || IsVkDown(VK_LCONTROL) || IsVkDown(VK_RCONTROL);
}
bool AnyAltDown()
{
    return IsVkDown(VK_MENU) || IsVkDown(VK_LMENU) || IsVkDown(VK_RMENU);
}
bool AnyShiftDown()
{
    return IsVkDown(VK_SHIFT) || IsVkDown(VK_LSHIFT) || IsVkDown(VK_RSHIFT);
}
bool AnyWinDown()
{
    return IsVkDown(VK_LWIN) || IsVkDown(VK_RWIN);
}

} // namespace

HotkeyManager::~HotkeyManager()
{
    UnregisterAll();
}

void HotkeyManager::SetBusy(bool busy)
{
    busy_ = busy;
    if (busy_ && pendingId_ != 0)
    {
        ClearPending(L"busy — cancelled armed hotkey");
    }
    QP_LOG_DEBUG(L"hotkey: busy=%d", busy_ ? 1 : 0);
}

void HotkeyManager::UnregisterAll()
{
    StopReleaseTimer();
    pendingId_ = 0;

    if (!hwnd_)
    {
        registered_.clear();
        return;
    }
    for (const auto& b : registered_)
    {
        if (b.id != 0)
        {
            if (!UnregisterHotKey(hwnd_, b.id))
            {
                QP_LOG_WARN(L"UnregisterHotKey id=%d failed: %s", b.id, LastErrorMessage().c_str());
            } else
            {
                QP_LOG_TRACE(L"UnregisterHotKey id=%d (%s) ok", b.id, b.hotkey.display.c_str());
            }
        }
    }
    registered_.clear();
}

bool HotkeyManager::RegisterAll(HWND hwnd, std::vector<HotkeyBinding> bindings, std::wstring* error)
{
    UnregisterAll();
    hwnd_ = hwnd;

    if (!hwnd_)
    {
        if (error)
            *error = L"null hwnd";
        return false;
    }

    int nextId = 1;
    std::vector<std::wstring> failures;

    for (auto& b : bindings)
    {
        b.id = nextId++;
        // Strip MOD_NOREPEAT from display already handled; ensure norepeat set.
        const UINT mods = b.hotkey.modifiers | MOD_NOREPEAT;
        const BOOL ok = RegisterHotKey(hwnd_, b.id, mods, b.hotkey.vk);
        if (!ok)
        {
            const std::wstring msg =
                b.hotkey.display + L" -> " + b.templateId + L"  (" + LastErrorMessage() + L")";
            QP_LOG_ERROR(L"RegisterHotKey FAILED: %s", msg.c_str());
            failures.push_back(msg);
            b.id = 0;
            continue;
        }
        QP_LOG_INFO(L"hotkey registered id=%d %s -> %s (%s) trigger=%s", b.id,
                    b.hotkey.display.c_str(), b.templateId.c_str(), b.label.c_str(),
                    HotkeyTriggerModeName(triggerMode_));
        registered_.push_back(std::move(b));
    }

    if (registered_.empty())
    {
        if (error)
        {
            *error = L"no hotkeys registered";
            if (!failures.empty())
            {
                *error += L": ";
                *error += failures.front();
            }
        }
        return false;
    }

    if (!failures.empty())
    {
        QP_LOG_WARN(L"%zu hotkey(s) failed to register, %zu ok", failures.size(),
                    registered_.size());
    }
    return true;
}

bool HotkeyManager::IsChordReleased(const HotkeyBinding& b) const
{
    // Trigger key must be up.
    if (IsVkDown(static_cast<int>(b.hotkey.vk)))
    {
        return false;
    }

    const UINT m = b.hotkey.modifiers & ~static_cast<UINT>(MOD_NOREPEAT);

    // Only require that modifiers *used in this chord* are up.
    // (Other stray keys are OK.)
    if ((m & MOD_CONTROL) && AnyControlDown())
        return false;
    if ((m & MOD_ALT) && AnyAltDown())
        return false;
    if ((m & MOD_SHIFT) && AnyShiftDown())
        return false;
    if ((m & MOD_WIN) && AnyWinDown())
        return false;

    return true;
}

void HotkeyManager::StartReleaseTimer()
{
    if (!hwnd_)
        return;
    // Reset interval each arm.
    SetTimer(hwnd_, kReleaseTimerId, static_cast<UINT>(releasePollMs_ > 0 ? releasePollMs_ : 15),
             nullptr);
}

void HotkeyManager::StopReleaseTimer()
{
    if (!hwnd_)
        return;
    KillTimer(hwnd_, kReleaseTimerId);
}

void HotkeyManager::ClearPending(const wchar_t* reason)
{
    if (pendingId_ == 0)
        return;
    QP_LOG_DEBUG(L"hotkey: clear pending id=%d (%s)", pendingId_, reason ? reason : L"");
    pendingId_ = 0;
    pendingSince_ = 0;
    StopReleaseTimer();
}

void HotkeyManager::FirePending(const wchar_t* reason)
{
    const int id = pendingId_;
    if (id == 0)
        return;

    const HotkeyBinding* b = FindById(id);
    pendingId_ = 0;
    pendingSince_ = 0;
    StopReleaseTimer();

    if (!b)
    {
        QP_LOG_WARN(L"hotkey: fire pending id=%d missing binding", id);
        return;
    }
    if (busy_)
    {
        QP_LOG_WARN(L"hotkey: fire suppressed (busy) id=%d %s", id, b->hotkey.display.c_str());
        return;
    }

    const DWORD held = pendingSince_ ? (GetTickCount() - pendingSince_) : 0;
    QP_LOG_INFO(L"hotkey: FIRE id=%d %s after %s (held ~%lu ms)", id, b->hotkey.display.c_str(),
                reason ? reason : L"release", static_cast<unsigned long>(held));

    if (callback_)
    {
        callback_(id, *b);
    }
}

void HotkeyManager::ArmPending(int id)
{
    const HotkeyBinding* b = FindById(id);
    if (!b)
    {
        QP_LOG_WARN(L"hotkey: arm unknown id=%d", id);
        return;
    }
    if (busy_)
    {
        QP_LOG_WARN(L"hotkey: ignore arm (busy) id=%d %s", id, b->hotkey.display.c_str());
        return;
    }

    // Replace any previously armed chord.
    if (pendingId_ != 0 && pendingId_ != id)
    {
        QP_LOG_DEBUG(L"hotkey: replacing pending id=%d with id=%d", pendingId_, id);
    }

    pendingId_ = id;
    pendingSince_ = GetTickCount();
    StartReleaseTimer();

    QP_LOG_DEBUG(L"hotkey: ARMED id=%d %s — waiting for release (timeout %d ms)", id,
                 b->hotkey.display.c_str(), releaseTimeoutMs_);
}

void HotkeyManager::OnWmHotkey(WPARAM wParam)
{
    const int id = static_cast<int>(wParam);
    const HotkeyBinding* b = FindById(id);
    if (!b)
    {
        QP_LOG_WARN(L"WM_HOTKEY unknown id=%d", id);
        return;
    }

    QP_LOG_DEBUG(L"WM_HOTKEY id=%d %s -> %s (mode=%s busy=%d)", id, b->hotkey.display.c_str(),
                 b->templateId.c_str(), HotkeyTriggerModeName(triggerMode_), busy_ ? 1 : 0);

    if (busy_)
    {
        QP_LOG_WARN(L"hotkey: ignored (workflow busy) %s", b->hotkey.display.c_str());
        return;
    }

    if (triggerMode_ == HotkeyTriggerMode::OnPress)
    {
        // Legacy immediate path.
        pendingId_ = id;
        pendingSince_ = GetTickCount();
        FirePending(L"on-press");
        return;
    }

    // OnRelease (default): arm and wait until chord is physically up.
    ArmPending(id);

    // Fast path: already released by the time we process the message (rare).
    if (IsChordReleased(*b))
    {
        FirePending(L"already-released");
    }
}

void HotkeyManager::OnTimer(WPARAM timerId)
{
    if (timerId != kReleaseTimerId)
        return;
    if (pendingId_ == 0)
    {
        StopReleaseTimer();
        return;
    }

    const HotkeyBinding* b = FindById(pendingId_);
    if (!b)
    {
        ClearPending(L"binding gone");
        return;
    }

    if (busy_)
    {
        ClearPending(L"became busy");
        return;
    }

    const DWORD elapsed = GetTickCount() - pendingSince_;

    if (IsChordReleased(*b))
    {
        FirePending(L"chord-released");
        return;
    }

    if (static_cast<int>(elapsed) >= releaseTimeoutMs_)
    {
        // User still holding — fire anyway so the chord isn't "eaten" forever.
        // Workflow will still synthesize modifier key-ups as a safety net.
        QP_LOG_WARN(L"hotkey: release timeout (%lu ms) id=%d %s — firing anyway",
                    static_cast<unsigned long>(elapsed), pendingId_, b->hotkey.display.c_str());
        FirePending(L"release-timeout");
        return;
    }

    // Still held; quiet poll (no per-tick log — would spam at 15ms).
}

const HotkeyBinding* HotkeyManager::FindById(int id) const
{
    for (const auto& b : registered_)
    {
        if (b.id == id)
            return &b;
    }
    return nullptr;
}

} // namespace qp
