#pragma once

#include <string>

namespace qp {

// Insert Unicode text into the foreground window via clipboard swap + Ctrl+V.
// Message-thread only. Heavy step logging for debug/trace.
class TextInjector {
public:
    explicit TextInjector(int pasteDelayMs = 80);

    bool Inject(const std::wstring& text, std::wstring* error = nullptr);
    void SetPasteDelayMs(int ms) { pasteDelayMs_ = ms; }

private:
    bool SaveClipboard(std::wstring* error);
    bool SetClipboardText(const std::wstring& text, std::wstring* error);
    bool RestoreClipboard(std::wstring* error);
    bool SendPaste(std::wstring* error);
    void ClearSaved();

    int pasteDelayMs_;
    bool saved_ = false;
    bool hadUnicode_ = false;
    std::wstring savedText_;
};

} // namespace qp
