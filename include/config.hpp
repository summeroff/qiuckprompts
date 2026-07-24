#pragma once

#include "logger.hpp"
#include "util.hpp"

#include <string>
#include <vector>

namespace qp {

// ---------------------------------------------------------------------------
// POC configuration — edit here. Later: simple text file (INI / key=value).
// ---------------------------------------------------------------------------

enum class ActionKind {
    // Paste template into the focused field only (old behavior).
    InsertTemplate = 0,
    // Select-all + copy from editor, open AI chat in browser, paste prompt+text.
    SendToAi = 1,
};

struct HotkeyBinding {
    HotkeySpec hotkey;
    std::wstring templateId;   // matches BuiltInTemplate::id
    std::wstring label;
    ActionKind action = ActionKind::SendToAi;
    std::wstring aiUrl;        // empty => WorkflowConfig::defaultAiUrl
    int id = 0;                // RegisterHotKey id
};

struct BuiltInTemplate {
    const wchar_t* id;
    const wchar_t* body;
};

inline HotkeySpec HK(UINT modifiers, UINT vk) {
    HotkeySpec h;
    h.modifiers = modifiers | MOD_NOREPEAT;
    h.vk = vk;
    h.display = FormatHotkeyDisplay(modifiers, vk);
    return h;
}

// ---- Built-in prompt templates --------------------------------------------

inline void GetBuiltInTemplates(const BuiltInTemplate*& outFirst, size_t& outCount) {
    static const BuiltInTemplate kTemplates[] = {
        {
            L"grammar_check",
            L"Please proofread the following text for grammar, spelling, and "
            L"clarity. Suggest corrections and a polished rewrite.\n\n"
        },
        {
            L"fact_check",
            L"Fact-check the following claims. For each claim list: verdict "
            L"(true / false / unclear), brief evidence, and sources if known.\n\n"
        },
        {
            L"summarize",
            L"Summarize the following text in 5 bullet points. Keep numbers "
            L"and proper names accurate.\n\n"
        },
        {
            L"explain_simple",
            L"Explain the following in simple terms, as if to a smart "
            L"non-expert. Use short paragraphs and one concrete example.\n\n"
        },
        {
            L"code_review",
            L"Review the following code. Focus on bugs, edge cases, readability, "
            L"and security. List findings by severity (blocker / major / nit).\n\n"
        },
    };
    outFirst = kTemplates;
    outCount = sizeof(kTemplates) / sizeof(kTemplates[0]);
}

inline bool FindTemplateBody(const std::wstring& id, std::wstring& out) {
    const BuiltInTemplate* first = nullptr;
    size_t n = 0;
    GetBuiltInTemplates(first, n);
    for (size_t i = 0; i < n; ++i) {
        if (id == first[i].id) {
            out = first[i].body;
            return true;
        }
    }
    return false;
}

// ---- Hotkeys: left hand Ctrl+Alt, right hand letter -----------------------
// J K L I O sit under the right hand while left holds Ctrl+Alt.

inline std::vector<HotkeyBinding> GetBuiltInBindings() {
    const UINT mod = MOD_CONTROL | MOD_ALT;
    std::vector<HotkeyBinding> v;
    v.push_back({ HK(mod, static_cast<UINT>('J')), L"grammar_check",  L"Grammar check",  ActionKind::SendToAi });
    v.push_back({ HK(mod, static_cast<UINT>('K')), L"fact_check",     L"Fact check",     ActionKind::SendToAi });
    v.push_back({ HK(mod, static_cast<UINT>('L')), L"summarize",      L"Summarize",      ActionKind::SendToAi });
    v.push_back({ HK(mod, static_cast<UINT>('I')), L"explain_simple", L"Explain simply", ActionKind::SendToAi });
    v.push_back({ HK(mod, static_cast<UINT>('O')), L"code_review",    L"Code review",    ActionKind::SendToAi });
    return v;
}

// ---- AI / browser workflow knobs ------------------------------------------

struct WorkflowConfig {
    // Prefer window/path matching this (case-insensitive). Empty = any Chrome/Edge.
    std::wstring browserTitleHint = L"Chrome Beta";

    // Default chat URL when binding.aiUrl is empty.
    std::wstring defaultAiUrl = L"https://www.meta.ai/";

    // Optional page-title substring to detect navigation (empty => derive from URL).
    // e.g. L"Meta", L"Gemini", L"ChatGPT"
    std::wstring pageTitleHint;

    // Timing (ms) for key steps.
    int afterModifierReleaseMs = 40;
    int afterSelectAllMs       = 40;
    int afterCopyMs            = 80;
    int afterActivateBrowserMs = 200;
    int afterNewTabMs          = 250;
    int afterUrlPasteMs        = 80;
    int afterFinalPasteMs      = 250;

    // Smart page-ready wait (replaces fixed 2.8s sleep).
    // Uses window title + UI Automation to find the chat Edit control.
    // Chrome web inputs are NOT Win32 HWNDs — only UIA can see them.
    int pageReadyTimeoutMs = 15000;  // hard stop
    int pageReadyPollMs    = 150;
    int pageReadyMinMs     = 500;    // never paste earlier than this after Enter
    int pageReadySettleMs  = 200;    // brief settle after ready
    bool pageReadyUseUia   = true;

    // If smart wait times out, still attempt paste (best-effort).
    bool pasteEvenIfNotReady = true;
};

// ---- Runtime options (CLI) ------------------------------------------------

struct AppConfig {
    std::wstring logPath;
    LogLevel logLevel = LogLevel::Debug;
    bool console = false;
    int pasteDelayMs = 200;
    WorkflowConfig workflow;
    // Global override: force InsertTemplate for every hotkey (debug).
    bool forceInsertOnly = false;
};

// Supported:
//   --console
//   --log-level=trace|debug|info|warn|error
//   --log-file=PATH
//   --paste-delay=MS
//   --ai-url=URL
//   --browser-hint=TEXT     (default "Chrome Beta")
//   --navigate-delay=MS
//   --insert-only          (skip browser flow; paste template only)
//   --self-test
bool ParseCommandLine(int argc, wchar_t** argv, AppConfig& cfg, std::wstring* error = nullptr);

inline const wchar_t* ActionKindName(ActionKind k) {
    switch (k) {
    case ActionKind::InsertTemplate: return L"InsertTemplate";
    case ActionKind::SendToAi:       return L"SendToAi";
    default:                         return L"?";
    }
}

} // namespace qp
