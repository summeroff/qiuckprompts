#pragma once

#include "logger.hpp"
#include "util.hpp"

#include <string>
#include <vector>

namespace qp {

// ---------------------------------------------------------------------------
// POC configuration — edit here. Later: load from a simple text file
// (likely INI-style key=value; no third-party parsers).
// ---------------------------------------------------------------------------

struct HotkeyBinding {
    HotkeySpec hotkey;
    std::wstring templateId;   // stable id, matches BuiltInTemplate::id
    std::wstring label;        // short name for logs / future UI
    int id = 0;                // RegisterHotKey id, filled at register time
};

struct BuiltInTemplate {
    const wchar_t* id;
    const wchar_t* body;
};

// Virtual-key helpers for readable bindings below.
// Digits: '1'..'9', '0'  |  letters: 'A'..'Z'  |  F-keys: VK_F1..
inline HotkeySpec HK(UINT modifiers, UINT vk) {
    HotkeySpec h;
    h.modifiers = modifiers | MOD_NOREPEAT;
    h.vk = vk;
    h.display = FormatHotkeyDisplay(modifiers, vk);
    return h;
}

// ---- Built-in prompt templates (POC) --------------------------------------

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

// ---- Built-in hotkey bindings (POC) ---------------------------------------
// Default chord: Ctrl+Alt+N  (avoids clashing with common app shortcuts)

inline std::vector<HotkeyBinding> GetBuiltInBindings() {
    std::vector<HotkeyBinding> v;
    v.push_back({ HK(MOD_CONTROL | MOD_ALT, static_cast<UINT>('1')), L"grammar_check",  L"Grammar check" });
    v.push_back({ HK(MOD_CONTROL | MOD_ALT, static_cast<UINT>('2')), L"fact_check",     L"Fact check" });
    v.push_back({ HK(MOD_CONTROL | MOD_ALT, static_cast<UINT>('3')), L"summarize",      L"Summarize" });
    v.push_back({ HK(MOD_CONTROL | MOD_ALT, static_cast<UINT>('4')), L"explain_simple", L"Explain simply" });
    v.push_back({ HK(MOD_CONTROL | MOD_ALT, static_cast<UINT>('5')), L"code_review",    L"Code review" });
    return v;
}

// ---- Runtime options (CLI / defaults only for POC) ------------------------

struct AppConfig {
    std::wstring logPath;                 // empty -> <exe>/logs/qiuckprompts.log
    LogLevel logLevel = LogLevel::Debug;  // verbose by default while developing
    bool console = false;                 // --console
    int pasteDelayMs = 80;                // clipboard restore delay after Ctrl+V
};

// Parse argv into cfg.
// Supported:
//   --console
//   --log-level=trace|debug|info|warn|error
//   --log-file=PATH
//   --paste-delay=MS
//   --self-test   (handled by main, not stored)
bool ParseCommandLine(int argc, wchar_t** argv, AppConfig& cfg, std::wstring* error = nullptr);

} // namespace qp
