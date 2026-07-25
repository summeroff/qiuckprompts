#pragma once

#include "logger.hpp"
#include "util.hpp"

#include <string>
#include <vector>

namespace qp {

enum class ActionKind {
    InsertTemplate = 0,
    SendToAi = 1,
};

enum class HotkeyTriggerMode {
    OnPress = 0,
    OnRelease = 1,
};

inline const wchar_t* HotkeyTriggerModeName(HotkeyTriggerMode m) {
    switch (m) {
    case HotkeyTriggerMode::OnPress:   return L"OnPress";
    case HotkeyTriggerMode::OnRelease: return L"OnRelease";
    default:                           return L"?";
    }
}

inline const wchar_t* ActionKindName(ActionKind k) {
    switch (k) {
    case ActionKind::InsertTemplate: return L"InsertTemplate";
    case ActionKind::SendToAi:       return L"SendToAi";
    default:                         return L"?";
    }
}

// One hotkey = one service URL + one prompt (+ optional image mode).
struct HotkeyBinding {
    HotkeySpec hotkey;
    std::wstring name;           // ini section id
    std::wstring label;
    std::wstring service;        // meta | gemini | grok | ...
    std::wstring templateId;     // alias of name (compat)
    ActionKind action = ActionKind::SendToAi;
    std::wstring aiUrl;
    std::wstring pageTitleHint;  // empty => derive from URL
    std::wstring promptBody;     // loaded from prompt file or inline
    bool captureEditor = true;
    bool requireClipboardImage = false;
    bool fenceEditorText = true;
    int id = 0;                  // RegisterHotKey id
};

struct WorkflowConfig {
    std::wstring browserTitleHint = L"Chrome";
    std::wstring defaultAiUrl = L"https://www.meta.ai/";
    std::wstring pageTitleHint;

    int afterModifierReleaseMs = 40;
    int afterSelectAllMs       = 40;
    int afterCopyMs            = 80;
    int afterActivateBrowserMs = 200;
    int afterNewTabMs          = 250;
    int afterUrlPasteMs        = 80;
    int afterFinalPasteMs      = 300;
    int afterImagePasteMs      = 350;

    int pageReadyTimeoutMs = 15000;
    int pageReadyPollMs    = 150;
    int pageReadyMinMs     = 500;
    int pageReadySettleMs  = 200;
    bool pageReadyUseUia   = true;
    bool pasteEvenIfNotReady = true;

    bool fenceEditorText = true;
    int clipboardRestoreDelayMs = 400;
};

struct AppConfig {
    std::wstring logPath;
    LogLevel logLevel = LogLevel::Debug;
    bool console = false;
    int pasteDelayMs = 200;
    WorkflowConfig workflow;
    bool forceInsertOnly = false;

    HotkeyTriggerMode hotkeyTrigger = HotkeyTriggerMode::OnRelease;
    int hotkeyReleaseTimeoutMs = 3000;
    int hotkeyReleasePollMs    = 15;

    std::wstring configPath;     // resolved ini path
    std::wstring configDir;      // directory containing ini + prompts/
    std::vector<HotkeyBinding> bindings;
};

inline HotkeySpec HK(UINT modifiers, UINT vk) {
    HotkeySpec h;
    h.modifiers = modifiers | MOD_NOREPEAT;
    h.vk = vk;
    h.display = FormatHotkeyDisplay(modifiers, vk);
    return h;
}

// Built-in fallback if no config file is found.
void GetBuiltinBindings(std::vector<HotkeyBinding>& out);

// Load ini + prompt files. Returns false on hard error (missing required keys).
// If path empty, tries <exe>/config/qiuckprompts.ini then <exe>/qiuckprompts.ini.
bool LoadConfigFile(const std::wstring& pathOrEmpty, AppConfig& cfg, std::wstring* error = nullptr);

bool ParseCommandLine(int argc, wchar_t** argv, AppConfig& cfg, std::wstring* error = nullptr);

// "Ctrl+Alt+J" → HotkeySpec
bool ParseHotkey(const std::wstring& text, HotkeySpec& out, std::wstring* error = nullptr);

// Expand {{TEXT}} / {{CONTEXT}} or fence-append editor body.
std::wstring BuildPromptPayload(const std::wstring& promptTemplate,
                                const std::wstring& editorText,
                                bool fenceEditorText);

} // namespace qp
