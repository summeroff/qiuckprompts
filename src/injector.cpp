#include "injector.hpp"
#include "logger.hpp"
#include "util.hpp"

#include <windows.h>

#include <cstring>
#include <vector>

namespace qp {

namespace {

bool OpenClipboardRetry(HWND owner, int attempts = 20, int sleepMs = 10) {
    for (int i = 0; i < attempts; ++i) {
        if (OpenClipboard(owner)) return true;
        Sleep(static_cast<DWORD>(sleepMs));
    }
    return false;
}

} // namespace

TextInjector::TextInjector(int pasteDelayMs)
    : pasteDelayMs_(pasteDelayMs) {}

void TextInjector::ClearSaved() {
    saved_ = false;
    hadUnicode_ = false;
    savedText_.clear();
}

bool TextInjector::SaveClipboard(std::wstring* error) {
    ClearSaved();
    QP_LOG_TRACE(L"clipboard: opening to save");

    if (!OpenClipboardRetry(nullptr)) {
        if (error) *error = L"OpenClipboard failed: " + LastErrorMessage();
        QP_LOG_ERROR(L"clipboard save: OpenClipboard failed: %s", LastErrorMessage().c_str());
        return false;
    }

    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h));
        if (p) {
            savedText_ = p;
            hadUnicode_ = true;
            GlobalUnlock(h);
            QP_LOG_DEBUG(L"clipboard: saved %zu wchar(s) of prior Unicode text",
                         savedText_.size());
        }
    } else {
        QP_LOG_DEBUG(L"clipboard: no CF_UNICODETEXT present (will not restore text)");
    }

    CloseClipboard();
    saved_ = true;
    return true;
}

bool TextInjector::SetClipboardText(const std::wstring& text, std::wstring* error) {
    QP_LOG_TRACE(L"clipboard: setting %zu wchar(s)", text.size());

    if (!OpenClipboardRetry(nullptr)) {
        if (error) *error = L"OpenClipboard failed: " + LastErrorMessage();
        QP_LOG_ERROR(L"clipboard set: OpenClipboard failed: %s", LastErrorMessage().c_str());
        return false;
    }

    if (!EmptyClipboard()) {
        const std::wstring msg = L"EmptyClipboard failed: " + LastErrorMessage();
        CloseClipboard();
        if (error) *error = msg;
        QP_LOG_ERROR(L"%s", msg.c_str());
        return false;
    }

    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        CloseClipboard();
        if (error) *error = L"GlobalAlloc failed";
        QP_LOG_ERROR(L"clipboard set: GlobalAlloc failed");
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
        const std::wstring msg = L"SetClipboardData failed: " + LastErrorMessage();
        GlobalFree(mem);
        CloseClipboard();
        if (error) *error = msg;
        QP_LOG_ERROR(L"%s", msg.c_str());
        return false;
    }
    // System owns mem after successful SetClipboardData.

    CloseClipboard();
    QP_LOG_DEBUG(L"clipboard: template text set");
    return true;
}

bool TextInjector::RestoreClipboard(std::wstring* error) {
    if (!saved_) return true;

    QP_LOG_TRACE(L"clipboard: restoring prior content (hadUnicode=%d)",
                 hadUnicode_ ? 1 : 0);

    if (!OpenClipboardRetry(nullptr)) {
        if (error) *error = L"OpenClipboard (restore) failed: " + LastErrorMessage();
        QP_LOG_WARN(L"clipboard restore: OpenClipboard failed: %s", LastErrorMessage().c_str());
        ClearSaved();
        return false;
    }

    EmptyClipboard();

    if (hadUnicode_) {
        const size_t bytes = (savedText_.size() + 1) * sizeof(wchar_t);
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (mem) {
            void* locked = GlobalLock(mem);
            if (locked) {
                memcpy(locked, savedText_.c_str(), bytes);
                GlobalUnlock(mem);
                if (!SetClipboardData(CF_UNICODETEXT, mem)) {
                    GlobalFree(mem);
                    QP_LOG_WARN(L"clipboard restore SetClipboardData failed: %s",
                                LastErrorMessage().c_str());
                }
            } else {
                GlobalFree(mem);
            }
        }
    }

    CloseClipboard();
    ClearSaved();
    QP_LOG_DEBUG(L"clipboard: restore done");
    return true;
}

bool TextInjector::SendPaste(std::wstring* error) {
    // Ctrl down, V down, V up, Ctrl up
    INPUT inputs[4]{};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'V';

    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    DWORD tid = fg ? GetWindowThreadProcessId(fg, &pid) : 0;
    QP_LOG_DEBUG(L"inject: SendInput Ctrl+V to foreground hwnd=%p pid=%lu tid=%lu",
                 fg, static_cast<unsigned long>(pid), static_cast<unsigned long>(tid));

    const UINT sent = SendInput(4, inputs, sizeof(INPUT));
    if (sent != 4) {
        const std::wstring msg = L"SendInput failed: " + LastErrorMessage();
        if (error) *error = msg;
        QP_LOG_ERROR(L"%s (sent %u/4)", msg.c_str(), sent);
        return false;
    }
    return true;
}

bool TextInjector::Inject(const std::wstring& text, std::wstring* error) {
    QP_LOG_INFO(L"inject: begin (%zu wchar)", text.size());

    if (text.empty()) {
        if (error) *error = L"empty template";
        QP_LOG_WARN(L"inject: empty text, skip");
        return false;
    }

    HWND fg = GetForegroundWindow();
    if (!fg) {
        if (error) *error = L"no foreground window";
        QP_LOG_WARN(L"inject: no foreground window");
        return false;
    }

    // Give the target a moment if the user just released the hotkey chord.
    // (Hotkeys use MOD_NOREPEAT; modifiers may still be physically down.)
    // Synthetic Ctrl+V while user still holds Ctrl+Alt can confuse some apps.
    // Release is handled by OS after hotkey; small settle delay helps.
    Sleep(30);

    if (!SaveClipboard(error)) {
        return false;
    }

    bool ok = false;
    if (SetClipboardText(text, error)) {
        if (SendPaste(error)) {
            if (pasteDelayMs_ > 0) {
                QP_LOG_TRACE(L"inject: waiting %d ms before clipboard restore", pasteDelayMs_);
                Sleep(static_cast<DWORD>(pasteDelayMs_));
            }
            ok = true;
        }
    }

    // Always attempt restore
    std::wstring restoreErr;
    if (!RestoreClipboard(&restoreErr)) {
        QP_LOG_WARN(L"inject: clipboard restore issue: %s", restoreErr.c_str());
    }

    if (ok) {
        QP_LOG_INFO(L"inject: success");
    } else {
        QP_LOG_ERROR(L"inject: failed%s",
                     error && !error->empty() ? (L": " + *error).c_str() : L"");
    }
    return ok;
}

} // namespace qp
