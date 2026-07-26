#include "injector.hpp"
#include "logger.hpp"
#include "util.hpp"

#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

namespace qp
{

namespace
{

bool OpenClipboardRetry(HWND owner, int attempts = 30, int sleepMs = 10)
{
    for (int i = 0; i < attempts; ++i)
    {
        if (OpenClipboard(owner))
            return true;
        Sleep(static_cast<DWORD>(sleepMs));
    }
    return false;
}

// Build a single keyboard INPUT (vk + optional scancode).
INPUT MakeKey(WORD vk, bool keyUp)
{
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = keyUp ? KEYEVENTF_KEYUP : 0;
    if (in.ki.wScan)
    {
        // scancode path is more reliable across layouts
        in.ki.dwFlags |= KEYEVENTF_SCANCODE;
        // For extended keys MapVirtualKey may need KEYEVENTF_EXTENDEDKEY;
        // Ctrl/V/menu/shift used here are not extended.
    }
    return in;
}

bool SendInputs(const INPUT* inputs, UINT count, std::wstring* error)
{
    const UINT sent = SendInput(count, const_cast<INPUT*>(inputs), sizeof(INPUT));
    if (sent != count)
    {
        const std::wstring msg = L"SendInput failed: " + LastErrorMessage();
        if (error)
            *error = msg;
        QP_LOG_ERROR(L"%s (sent %u/%u)", msg.c_str(), sent, count);
        return false;
    }
    return true;
}

// Force key-up for modifiers so a following Ctrl+V is not Ctrl+Alt+V etc.
// Physical keys may still be down; we also wait for release afterwards.
bool ReleaseModifiers(std::wstring* error)
{
    // Both left and right variants + generic
    const WORD mods[] = {
        VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU,
        VK_SHIFT,   VK_LSHIFT,   VK_RSHIFT,   VK_LWIN, VK_RWIN,
    };

    std::vector<INPUT> ups;
    ups.reserve(16);

    for (WORD vk : mods)
    {
        // If down (high bit), synthesize up.
        if (GetAsyncKeyState(vk) & 0x8000)
        {
            QP_LOG_DEBUG(L"inject: synthesizing KEYUP for vk=0x%02X (still down)", vk);
            ups.push_back(MakeKey(vk, true));
        }
    }

    if (ups.empty())
    {
        QP_LOG_TRACE(L"inject: no modifiers down at start of inject");
        return true;
    }
    return SendInputs(ups.data(), static_cast<UINT>(ups.size()), error);
}

// Block until modifiers are physically up, or timeout.
bool WaitModifiersReleased(int timeoutMs)
{
    const WORD mods[] = {
        VK_CONTROL, VK_LCONTROL, VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU,
        VK_SHIFT,   VK_LSHIFT,   VK_RSHIFT,   VK_LWIN, VK_RWIN,
    };

    const DWORD start = GetTickCount();
    for (;;)
    {
        bool anyDown = false;
        for (WORD vk : mods)
        {
            if (GetAsyncKeyState(vk) & 0x8000)
            {
                anyDown = true;
                break;
            }
        }
        if (!anyDown)
        {
            QP_LOG_DEBUG(L"inject: modifiers released after %lu ms",
                         static_cast<unsigned long>(GetTickCount() - start));
            return true;
        }
        if (static_cast<int>(GetTickCount() - start) >= timeoutMs)
        {
            QP_LOG_WARN(L"inject: timed out waiting for modifier release (%d ms)", timeoutMs);
            return false;
        }
        Sleep(10);
    }
}

struct FocusInfo
{
    HWND foreground = nullptr;
    HWND focus = nullptr; // control with keyboard focus (may equal foreground)
    DWORD pid = 0;
    DWORD tid = 0;
    std::wstring fgTitle;
    std::wstring fgClass;
    std::wstring focusClass;
};

std::wstring WindowClassName(HWND hwnd)
{
    if (!hwnd)
        return {};
    wchar_t buf[256]{};
    GetClassNameW(hwnd, buf, 256);
    return buf;
}

std::wstring WindowTitle(HWND hwnd)
{
    if (!hwnd)
        return {};
    wchar_t buf[256]{};
    GetWindowTextW(hwnd, buf, 256);
    return buf;
}

FocusInfo CaptureFocusInfo()
{
    FocusInfo info;
    info.foreground = GetForegroundWindow();
    if (!info.foreground)
        return info;

    info.tid = GetWindowThreadProcessId(info.foreground, &info.pid);
    info.fgTitle = WindowTitle(info.foreground);
    info.fgClass = WindowClassName(info.foreground);

    // Resolve the actual focused child control.
    GUITHREADINFO gi{};
    gi.cbSize = sizeof(gi);
    if (GetGUIThreadInfo(info.tid, &gi) && gi.hwndFocus)
    {
        info.focus = gi.hwndFocus;
    } else
    {
        // Attach to read GetFocus() of target thread.
        const DWORD ourTid = GetCurrentThreadId();
        if (info.tid && info.tid != ourTid)
        {
            if (AttachThreadInput(ourTid, info.tid, TRUE))
            {
                info.focus = GetFocus();
                AttachThreadInput(ourTid, info.tid, FALSE);
            }
        }
        if (!info.focus)
            info.focus = info.foreground;
    }
    info.focusClass = WindowClassName(info.focus);

    QP_LOG_DEBUG(L"inject: fg=%p class='%s' title='%s' pid=%lu", info.foreground,
                 info.fgClass.c_str(), info.fgTitle.c_str(), static_cast<unsigned long>(info.pid));
    QP_LOG_DEBUG(L"inject: focus=%p class='%s'", info.focus, info.focusClass.c_str());
    return info;
}

// Native edit controls often accept WM_PASTE without needing synthetic keys.
bool TryWmPaste(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return false;

    const std::wstring cls = WindowClassName(hwnd);
    QP_LOG_DEBUG(L"inject: SendMessageTimeout WM_PASTE to %p ('%s')", hwnd, cls.c_str());

    DWORD_PTR result = 0;
    const LRESULT ok =
        SendMessageTimeoutW(hwnd, WM_PASTE, 0, 0, SMTO_ABORTIFHUNG | SMTO_NORMAL, 500, &result);
    if (!ok)
    {
        QP_LOG_DEBUG(L"inject: WM_PASTE timed out or failed: %s", LastErrorMessage().c_str());
        return false;
    }
    (void)result;
    return true;
}

bool IsNativeEditable(const std::wstring& cls)
{
    if (cls.empty())
        return false;
    return cls == L"Edit" || cls == L"RichEdit" || cls == L"RichEdit20A" || cls == L"RichEdit20W" ||
           cls == L"RichEdit50W" || cls == L"RICHCEDIT50W";
}

} // namespace

TextInjector::TextInjector(int pasteDelayMs) : pasteDelayMs_(pasteDelayMs)
{
}

void TextInjector::ClearSaved()
{
    saved_ = false;
    hadUnicode_ = false;
    savedText_.clear();
}

bool TextInjector::SaveClipboard(std::wstring* error)
{
    ClearSaved();
    QP_LOG_TRACE(L"clipboard: opening to save");

    if (!OpenClipboardRetry(nullptr))
    {
        if (error)
            *error = L"OpenClipboard failed: " + LastErrorMessage();
        QP_LOG_ERROR(L"clipboard save: OpenClipboard failed: %s", LastErrorMessage().c_str());
        return false;
    }

    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h)
    {
        const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h));
        if (p)
        {
            savedText_ = p;
            hadUnicode_ = true;
            GlobalUnlock(h);
            QP_LOG_DEBUG(L"clipboard: saved %zu wchar(s) of prior Unicode text", savedText_.size());
        }
    } else
    {
        QP_LOG_DEBUG(L"clipboard: no CF_UNICODETEXT present (will not restore text)");
    }

    CloseClipboard();
    saved_ = true;
    return true;
}

bool TextInjector::SetClipboardText(const std::wstring& text, std::wstring* error)
{
    QP_LOG_TRACE(L"clipboard: setting %zu wchar(s)", text.size());

    if (!OpenClipboardRetry(nullptr))
    {
        if (error)
            *error = L"OpenClipboard failed: " + LastErrorMessage();
        QP_LOG_ERROR(L"clipboard set: OpenClipboard failed: %s", LastErrorMessage().c_str());
        return false;
    }

    if (!EmptyClipboard())
    {
        const std::wstring msg = L"EmptyClipboard failed: " + LastErrorMessage();
        CloseClipboard();
        if (error)
            *error = msg;
        QP_LOG_ERROR(L"%s", msg.c_str());
        return false;
    }

    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem)
    {
        CloseClipboard();
        if (error)
            *error = L"GlobalAlloc failed";
        QP_LOG_ERROR(L"clipboard set: GlobalAlloc failed");
        return false;
    }

    void* locked = GlobalLock(mem);
    if (!locked)
    {
        GlobalFree(mem);
        CloseClipboard();
        if (error)
            *error = L"GlobalLock failed";
        return false;
    }
    memcpy(locked, text.c_str(), bytes);
    GlobalUnlock(mem);

    if (!SetClipboardData(CF_UNICODETEXT, mem))
    {
        const std::wstring msg = L"SetClipboardData failed: " + LastErrorMessage();
        GlobalFree(mem);
        CloseClipboard();
        if (error)
            *error = msg;
        QP_LOG_ERROR(L"%s", msg.c_str());
        return false;
    }

    CloseClipboard();
    QP_LOG_DEBUG(L"clipboard: template text set");
    return true;
}

bool TextInjector::RestoreClipboard(std::wstring* error)
{
    if (!saved_)
        return true;

    QP_LOG_TRACE(L"clipboard: restoring prior content (hadUnicode=%d)", hadUnicode_ ? 1 : 0);

    if (!OpenClipboardRetry(nullptr))
    {
        if (error)
            *error = L"OpenClipboard (restore) failed: " + LastErrorMessage();
        QP_LOG_WARN(L"clipboard restore: OpenClipboard failed: %s", LastErrorMessage().c_str());
        ClearSaved();
        return false;
    }

    EmptyClipboard();

    if (hadUnicode_)
    {
        const size_t bytes = (savedText_.size() + 1) * sizeof(wchar_t);
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (mem)
        {
            void* locked = GlobalLock(mem);
            if (locked)
            {
                memcpy(locked, savedText_.c_str(), bytes);
                GlobalUnlock(mem);
                if (!SetClipboardData(CF_UNICODETEXT, mem))
                {
                    GlobalFree(mem);
                    QP_LOG_WARN(L"clipboard restore SetClipboardData failed: %s",
                                LastErrorMessage().c_str());
                }
            } else
            {
                GlobalFree(mem);
            }
        }
    }

    CloseClipboard();
    ClearSaved();
    QP_LOG_DEBUG(L"clipboard: restore done");
    return true;
}

bool TextInjector::SendPaste(std::wstring* error)
{
    // Clean Ctrl+V with scancodes. Assumes modifiers already released.
    INPUT inputs[4] = {
        MakeKey(VK_CONTROL, false),
        MakeKey('V', false),
        MakeKey('V', true),
        MakeKey(VK_CONTROL, true),
    };

    QP_LOG_DEBUG(L"inject: SendInput Ctrl+V (scancode)");
    return SendInputs(inputs, 4, error);
}

bool TextInjector::SendUnicodeText(const std::wstring& text, std::wstring* error)
{
    if (text.empty())
        return false;

    // KEYEVENTF_UNICODE path — works even when clipboard paste is blocked.
    // Each char: key down + key up with wScan = code unit, wVk = 0.
    std::vector<INPUT> inputs;
    inputs.reserve(text.size() * 2);

    for (wchar_t ch : text)
    {
        INPUT down{};
        down.type = INPUT_KEYBOARD;
        down.ki.wVk = 0;
        down.ki.wScan = ch;
        down.ki.dwFlags = KEYEVENTF_UNICODE;

        INPUT up = down;
        up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

        inputs.push_back(down);
        inputs.push_back(up);
    }

    QP_LOG_DEBUG(L"inject: SendInput UNICODE %zu code-unit(s) (%zu events)", text.size(),
                 inputs.size());

    // Send in chunks to avoid huge single calls.
    constexpr UINT kChunk = 64; // 32 chars
    UINT offset = 0;
    const UINT total = static_cast<UINT>(inputs.size());
    while (offset < total)
    {
        const UINT n = (total - offset > kChunk) ? kChunk : (total - offset);
        if (!SendInputs(inputs.data() + offset, n, error))
        {
            return false;
        }
        offset += n;
    }
    return true;
}

bool TextInjector::Inject(const std::wstring& text, std::wstring* error)
{
    QP_LOG_INFO(L"inject: begin (%zu wchar)", text.size());

    if (text.empty())
    {
        if (error)
            *error = L"empty template";
        QP_LOG_WARN(L"inject: empty text, skip");
        return false;
    }

    // 1) Clear modifier state from the hotkey chord (Ctrl+Alt+N still held).
    //    Without this, synthetic Ctrl+V becomes Ctrl+Alt+V and most apps ignore it.
    if (!ReleaseModifiers(error))
    {
        QP_LOG_WARN(L"inject: ReleaseModifiers had SendInput issues (continuing)");
    }
    WaitModifiersReleased(500);

    // Extra settle so the target app finishes processing the physical keyups.
    Sleep(40);

    const FocusInfo focus = CaptureFocusInfo();
    if (!focus.foreground)
    {
        if (error)
            *error = L"no foreground window";
        QP_LOG_WARN(L"inject: no foreground window");
        return false;
    }

    // 2) Clipboard swap
    if (!SaveClipboard(error))
    {
        return false;
    }

    if (!SetClipboardText(text, error))
    {
        RestoreClipboard(nullptr);
        return false;
    }

    // 3) Deliver paste — one primary method to avoid double-insert.
    //    Native Edit/RichEdit: WM_PASTE is reliable and doesn't need key state.
    //    Everything else (browsers, Electron, Office, ...): Ctrl+V via SendInput.
    bool pasted = false;
    const bool nativeEdit = IsNativeEditable(focus.focusClass);

    if (nativeEdit && focus.focus)
    {
        QP_LOG_DEBUG(L"inject: strategy=WM_PASTE (native edit class)");
        pasted = TryWmPaste(focus.focus);
        if (!pasted)
        {
            QP_LOG_DEBUG(L"inject: WM_PASTE failed, falling back to Ctrl+V");
        }
    }

    if (!pasted)
    {
        QP_LOG_DEBUG(L"inject: strategy=Ctrl+V SendInput");
        // Ensure modifiers still up right before key chord
        ReleaseModifiers(nullptr);
        WaitModifiersReleased(200);
        if (SendPaste(error))
        {
            pasted = true;
        }
    }

    // Give the target time to read the clipboard BEFORE we restore it.
    const int delay = pasteDelayMs_ > 0 ? pasteDelayMs_ : 200;
    QP_LOG_TRACE(L"inject: waiting %d ms before clipboard restore", delay);
    Sleep(static_cast<DWORD>(delay));

    // 4) Unicode fallback only if both paste strategies failed at SendInput/message level.
    if (!pasted)
    {
        QP_LOG_WARN(L"inject: paste path failed, trying UNICODE fallback");
        RestoreClipboard(nullptr);
        // Modifiers must be up for unicode injection too
        ReleaseModifiers(nullptr);
        WaitModifiersReleased(200);
        const bool uni = SendUnicodeText(text, error);
        if (uni)
        {
            QP_LOG_INFO(L"inject: success (unicode fallback)");
            return true;
        }
        QP_LOG_ERROR(L"inject: all methods failed");
        return false;
    }

    std::wstring restoreErr;
    if (!RestoreClipboard(&restoreErr))
    {
        QP_LOG_WARN(L"inject: clipboard restore issue: %s", restoreErr.c_str());
    }

    QP_LOG_INFO(L"inject: success");
    return true;
}

} // namespace qp
