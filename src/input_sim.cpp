#include "input_sim.hpp"
#include "logger.hpp"
#include "util.hpp"

#include <cstring>
#include <vector>

namespace qp {

namespace {

bool OpenClipboardRetry(HWND owner, int attempts = 30, int sleepMs = 10) {
    for (int i = 0; i < attempts; ++i) {
        if (OpenClipboard(owner)) return true;
        Sleep(static_cast<DWORD>(sleepMs));
    }
    return false;
}

INPUT MakeVk(WORD vk, bool keyUp) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = keyUp ? KEYEVENTF_KEYUP : 0;
    return in;
}

bool SendInputs(std::vector<INPUT>& inputs, std::wstring* error) {
    if (inputs.empty()) return true;
    const UINT sent = SendInput(static_cast<UINT>(inputs.size()),
                                inputs.data(), sizeof(INPUT));
    if (sent != inputs.size()) {
        const std::wstring msg = L"SendInput failed: " + LastErrorMessage();
        if (error) *error = msg;
        QP_LOG_ERROR(L"%s (sent %u/%zu)", msg.c_str(), sent, inputs.size());
        return false;
    }
    return true;
}

std::wstring WindowClassName(HWND hwnd) {
    if (!hwnd) return {};
    wchar_t buf[256]{};
    GetClassNameW(hwnd, buf, 256);
    return buf;
}

std::wstring WindowTitle(HWND hwnd) {
    if (!hwnd) return {};
    wchar_t buf[512]{};
    GetWindowTextW(hwnd, buf, 512);
    return buf;
}

} // namespace

bool ReleaseModifiers(std::wstring* error) {
    const WORD mods[] = {
        VK_CONTROL, VK_LCONTROL, VK_RCONTROL,
        VK_MENU,    VK_LMENU,    VK_RMENU,
        VK_SHIFT,   VK_LSHIFT,   VK_RSHIFT,
        VK_LWIN,    VK_RWIN,
    };
    std::vector<INPUT> ups;
    for (WORD vk : mods) {
        if (GetAsyncKeyState(vk) & 0x8000) {
            QP_LOG_DEBUG(L"input: KEYUP vk=0x%02X (was down)", vk);
            ups.push_back(MakeVk(vk, true));
        }
    }
    if (ups.empty()) return true;
    return SendInputs(ups, error);
}

bool WaitModifiersReleased(int timeoutMs) {
    const WORD mods[] = {
        VK_CONTROL, VK_LCONTROL, VK_RCONTROL,
        VK_MENU,    VK_LMENU,    VK_RMENU,
        VK_SHIFT,   VK_LSHIFT,   VK_RSHIFT,
        VK_LWIN,    VK_RWIN,
    };
    const DWORD start = GetTickCount();
    for (;;) {
        bool any = false;
        for (WORD vk : mods) {
            if (GetAsyncKeyState(vk) & 0x8000) { any = true; break; }
        }
        if (!any) {
            QP_LOG_TRACE(L"input: modifiers up after %lu ms",
                         static_cast<unsigned long>(GetTickCount() - start));
            return true;
        }
        if (static_cast<int>(GetTickCount() - start) >= timeoutMs) {
            QP_LOG_WARN(L"input: modifier release timeout (%d ms)", timeoutMs);
            return false;
        }
        Sleep(10);
    }
}

bool SendKeyChord(UINT modifiers, WORD vk, std::wstring* error) {
    std::vector<INPUT> seq;
    auto pushDown = [&](WORD k) { seq.push_back(MakeVk(k, false)); };
    auto pushUp   = [&](WORD k) { seq.push_back(MakeVk(k, true)); };

    if (modifiers & MOD_CONTROL) pushDown(VK_CONTROL);
    if (modifiers & MOD_ALT)     pushDown(VK_MENU);
    if (modifiers & MOD_SHIFT)   pushDown(VK_SHIFT);
    if (modifiers & MOD_WIN)     pushDown(VK_LWIN);

    pushDown(vk);
    pushUp(vk);

    if (modifiers & MOD_WIN)     pushUp(VK_LWIN);
    if (modifiers & MOD_SHIFT)   pushUp(VK_SHIFT);
    if (modifiers & MOD_ALT)     pushUp(VK_MENU);
    if (modifiers & MOD_CONTROL) pushUp(VK_CONTROL);

    QP_LOG_DEBUG(L"input: chord mods=0x%X vk=0x%02X (%zu events)",
                 modifiers, vk, seq.size());
    return SendInputs(seq, error);
}

bool SendCopy(std::wstring* error)      { return SendKeyChord(MOD_CONTROL, 'C', error); }
bool SendPaste(std::wstring* error)     { return SendKeyChord(MOD_CONTROL, 'V', error); }
bool SendSelectAll(std::wstring* error) { return SendKeyChord(MOD_CONTROL, 'A', error); }
bool SendNewTab(std::wstring* error)    { return SendKeyChord(MOD_CONTROL, 'T', error); }
bool SendFocusOmnibox(std::wstring* error) { return SendKeyChord(MOD_CONTROL, 'L', error); }

bool SendEnter(std::wstring* error) {
    std::vector<INPUT> seq = { MakeVk(VK_RETURN, false), MakeVk(VK_RETURN, true) };
    return SendInputs(seq, error);
}

bool SendUnicodeText(const std::wstring& text, std::wstring* error) {
    if (text.empty()) return true;
    std::vector<INPUT> inputs;
    inputs.reserve(text.size() * 2);
    for (wchar_t ch : text) {
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
    QP_LOG_DEBUG(L"input: unicode %zu wchar", text.size());
    // chunk
    constexpr size_t kChunk = 64;
    for (size_t off = 0; off < inputs.size(); off += kChunk) {
        const size_t n = (inputs.size() - off > kChunk) ? kChunk : (inputs.size() - off);
        std::vector<INPUT> slice(inputs.begin() + static_cast<std::ptrdiff_t>(off),
                                 inputs.begin() + static_cast<std::ptrdiff_t>(off + n));
        if (!SendInputs(slice, error)) return false;
    }
    return true;
}

bool ClipboardReadUnicode(std::wstring& out, std::wstring* error) {
    out.clear();
    if (!OpenClipboardRetry(nullptr)) {
        if (error) *error = L"OpenClipboard read failed: " + LastErrorMessage();
        return false;
    }
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h));
        if (p) {
            out = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    QP_LOG_DEBUG(L"clipboard: read %zu wchar", out.size());
    return true;
}

bool ClipboardWriteUnicode(const std::wstring& text, std::wstring* error) {
    if (!OpenClipboardRetry(nullptr)) {
        if (error) *error = L"OpenClipboard write failed: " + LastErrorMessage();
        return false;
    }
    if (!EmptyClipboard()) {
        CloseClipboard();
        if (error) *error = L"EmptyClipboard failed: " + LastErrorMessage();
        return false;
    }
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        CloseClipboard();
        if (error) *error = L"GlobalAlloc failed";
        return false;
    }
    void* locked = GlobalLock(mem);
    if (!locked) {
        GlobalFree(mem);
        CloseClipboard();
        if (error) *error = L"GlobalLock failed";
        return false;
    }
    memcpy(locked, text.c_str(), bytes);
    GlobalUnlock(mem);
    if (!SetClipboardData(CF_UNICODETEXT, mem)) {
        GlobalFree(mem);
        CloseClipboard();
        if (error) *error = L"SetClipboardData failed: " + LastErrorMessage();
        return false;
    }
    CloseClipboard();
    QP_LOG_DEBUG(L"clipboard: wrote %zu wchar", text.size());
    return true;
}

bool ForceForeground(HWND hwnd, std::wstring* error) {
    if (!hwnd || !IsWindow(hwnd)) {
        if (error) *error = L"invalid hwnd";
        return false;
    }

    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }

    HWND fg = GetForegroundWindow();
    if (fg == hwnd) {
        QP_LOG_TRACE(L"focus: already foreground %p", hwnd);
        return true;
    }

    const DWORD targetTid = GetWindowThreadProcessId(hwnd, nullptr);
    const DWORD fgTid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD ourTid = GetCurrentThreadId();

    // Attach to both FG and target threads so SetForegroundWindow is allowed.
    bool attachedFg = false;
    bool attachedTarget = false;
    if (fgTid && fgTid != ourTid) {
        attachedFg = AttachThreadInput(ourTid, fgTid, TRUE) != 0;
    }
    if (targetTid && targetTid != ourTid && targetTid != fgTid) {
        attachedTarget = AttachThreadInput(ourTid, targetTid, TRUE) != 0;
    }

    BringWindowToTop(hwnd);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    const BOOL ok = SetForegroundWindow(hwnd);

    if (attachedTarget) AttachThreadInput(ourTid, targetTid, FALSE);
    if (attachedFg) AttachThreadInput(ourTid, fgTid, FALSE);

    // Nudge: empty Alt press sometimes unlocks foreground lock.
    if (GetForegroundWindow() != hwnd) {
        QP_LOG_DEBUG(L"focus: Alt-nudge then retry SetForegroundWindow");
        keybd_event(VK_MENU, 0, 0, 0);
        keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
        SetForegroundWindow(hwnd);
    }

    const HWND now = GetForegroundWindow();
    const bool success = (now == hwnd);
    QP_LOG_DEBUG(L"focus: ForceForeground hwnd=%p ok=%d fgNow=%p SetFG=%d",
                 hwnd, success ? 1 : 0, now, ok ? 1 : 0);
    if (!success && error) {
        *error = L"SetForegroundWindow did not stick";
    }
    return success;
}

FocusSnapshot CaptureFocusSnapshot() {
    FocusSnapshot info;
    info.foreground = GetForegroundWindow();
    if (!info.foreground) return info;

    info.tid = GetWindowThreadProcessId(info.foreground, &info.pid);
    info.fgTitle = WindowTitle(info.foreground);
    info.fgClass = WindowClassName(info.foreground);

    GUITHREADINFO gi{};
    gi.cbSize = sizeof(gi);
    if (GetGUIThreadInfo(info.tid, &gi) && gi.hwndFocus) {
        info.focus = gi.hwndFocus;
    } else {
        const DWORD ourTid = GetCurrentThreadId();
        if (info.tid && info.tid != ourTid) {
            if (AttachThreadInput(ourTid, info.tid, TRUE)) {
                info.focus = GetFocus();
                AttachThreadInput(ourTid, info.tid, FALSE);
            }
        }
        if (!info.focus) info.focus = info.foreground;
    }
    info.focusClass = WindowClassName(info.focus);

    QP_LOG_DEBUG(L"focus: fg=%p '%s' class='%s' pid=%lu focus=%p class='%s'",
                 info.foreground, info.fgTitle.c_str(), info.fgClass.c_str(),
                 static_cast<unsigned long>(info.pid),
                 info.focus, info.focusClass.c_str());
    return info;
}

} // namespace qp
