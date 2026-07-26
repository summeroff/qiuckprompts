#pragma once

#include <string>

namespace qp
{

// Insert Unicode text into the foreground window.
// Strategy:
//   1) release hotkey modifiers (critical — else Ctrl+V becomes Ctrl+Alt+V)
//   2) clipboard set
//   3) WM_PASTE to focused control
//   4) SendInput Ctrl+V (scancodes)
//   5) delayed clipboard restore
//   6) KEYEVENTF_UNICODE fallback if paste SendInput failed
// Message-thread only.
class TextInjector
{
public:
    explicit TextInjector(int pasteDelayMs = 200);

    bool Inject(const std::wstring& text, std::wstring* error = nullptr);
    void SetPasteDelayMs(int ms) { pasteDelayMs_ = ms; }

private:
    bool SaveClipboard(std::wstring* error);
    bool SetClipboardText(const std::wstring& text, std::wstring* error);
    bool RestoreClipboard(std::wstring* error);
    bool SendPaste(std::wstring* error);
    bool SendUnicodeText(const std::wstring& text, std::wstring* error);
    void ClearSaved();

    int pasteDelayMs_;
    bool saved_ = false;
    bool hadUnicode_ = false;
    std::wstring savedText_;
};

} // namespace qp
