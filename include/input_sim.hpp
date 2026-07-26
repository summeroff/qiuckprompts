#pragma once

#include <windows.h>

#include <string>

namespace qp
{

// Low-level input helpers shared by injector + AI workflow.
// All of these are message-thread oriented; heavy logging inside.

bool ReleaseModifiers(std::wstring* error = nullptr);
bool WaitModifiersReleased(int timeoutMs);

// Ctrl/Alt/Shift + key chord (down modifiers, down key, up key, up modifiers).
bool SendKeyChord(UINT modifiers, WORD vk, std::wstring* error = nullptr);

// Convenience
bool SendCopy(std::wstring* error = nullptr);         // Ctrl+C
bool SendPaste(std::wstring* error = nullptr);        // Ctrl+V
bool SendSelectAll(std::wstring* error = nullptr);    // Ctrl+A
bool SendNewTab(std::wstring* error = nullptr);       // Ctrl+T
bool SendFocusOmnibox(std::wstring* error = nullptr); // Ctrl+L
bool SendEnter(std::wstring* error = nullptr);

// KEYEVENTF_UNICODE typing (no clipboard).
bool SendUnicodeText(const std::wstring& text, std::wstring* error = nullptr);

// Clipboard
bool ClipboardReadUnicode(std::wstring& out, std::wstring* error = nullptr);
bool ClipboardWriteUnicode(const std::wstring& text, std::wstring* error = nullptr);

// Bring hwnd to foreground (AttachThreadInput dance). Returns false if still not FG.
bool ForceForeground(HWND hwnd, std::wstring* error = nullptr);

struct FocusSnapshot
{
    HWND foreground = nullptr;
    HWND focus = nullptr;
    DWORD pid = 0;
    DWORD tid = 0;
    std::wstring fgTitle;
    std::wstring fgClass;
    std::wstring focusClass;
};

FocusSnapshot CaptureFocusSnapshot();

} // namespace qp
