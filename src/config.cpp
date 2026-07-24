#include "config.hpp"
#include "logger.hpp"

namespace qp {

namespace {

bool TakeEqValue(int& i, int argc, wchar_t** argv,
                 const std::wstring& arg, const wchar_t* flag,
                 std::wstring& out) {
    const std::wstring prefix = std::wstring(flag) + L"=";
    if (arg.rfind(prefix, 0) == 0) {
        out = arg.substr(prefix.size());
        return true;
    }
    if (arg == flag) {
        if (i + 1 < argc && argv[i + 1]) {
            out = argv[++i];
            return true;
        }
    }
    return false;
}

int ClampInt(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

} // namespace

bool ParseCommandLine(int argc, wchar_t** argv, AppConfig& cfg, std::wstring* error) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i] ? argv[i] : L"";
        if (arg.empty()) continue;

        if (arg == L"--console") {
            cfg.console = true;
            continue;
        }
        if (arg == L"--self-test" || arg == L"--help" || arg == L"-h" || arg == L"/?") {
            continue;
        }
        if (arg == L"--insert-only") {
            cfg.forceInsertOnly = true;
            continue;
        }
        if (arg == L"--no-uia") {
            cfg.workflow.pageReadyUseUia = false;
            continue;
        }
        if (arg == L"--hotkey-on-press") {
            cfg.hotkeyTrigger = HotkeyTriggerMode::OnPress;
            continue;
        }
        if (arg == L"--hotkey-on-release") {
            cfg.hotkeyTrigger = HotkeyTriggerMode::OnRelease;
            continue;
        }

        std::wstring v;
        if (TakeEqValue(i, argc, argv, arg, L"--log-level", v)) {
            if (v.empty()) {
                if (error) *error = L"--log-level requires a value";
                return false;
            }
            cfg.logLevel = Logger::ParseLevel(v);
            continue;
        }
        if (TakeEqValue(i, argc, argv, arg, L"--log-file", v)) {
            if (v.empty()) {
                if (error) *error = L"--log-file requires a path";
                return false;
            }
            cfg.logPath = v;
            continue;
        }
        if (TakeEqValue(i, argc, argv, arg, L"--paste-delay", v)) {
            cfg.pasteDelayMs = ClampInt(_wtoi(v.c_str()), 0, 5000);
            continue;
        }
        if (TakeEqValue(i, argc, argv, arg, L"--ai-url", v)) {
            if (v.empty()) {
                if (error) *error = L"--ai-url requires a URL";
                return false;
            }
            cfg.workflow.defaultAiUrl = v;
            continue;
        }
        if (TakeEqValue(i, argc, argv, arg, L"--browser-hint", v)) {
            cfg.workflow.browserTitleHint = v;
            continue;
        }
        if (TakeEqValue(i, argc, argv, arg, L"--page-title-hint", v)) {
            cfg.workflow.pageTitleHint = v;
            continue;
        }
        // Back-compat alias: --navigate-delay sets page-ready timeout
        if (TakeEqValue(i, argc, argv, arg, L"--navigate-delay", v) ||
            TakeEqValue(i, argc, argv, arg, L"--page-ready-timeout", v)) {
            cfg.workflow.pageReadyTimeoutMs = ClampInt(_wtoi(v.c_str()), 500, 120000);
            continue;
        }
        if (TakeEqValue(i, argc, argv, arg, L"--page-ready-min", v)) {
            cfg.workflow.pageReadyMinMs = ClampInt(_wtoi(v.c_str()), 0, 10000);
            continue;
        }
        if (TakeEqValue(i, argc, argv, arg, L"--hotkey-release-timeout", v)) {
            cfg.hotkeyReleaseTimeoutMs = ClampInt(_wtoi(v.c_str()), 100, 30000);
            continue;
        }
        if (TakeEqValue(i, argc, argv, arg, L"--hotkey-release-poll", v)) {
            cfg.hotkeyReleasePollMs = ClampInt(_wtoi(v.c_str()), 5, 100);
            continue;
        }
    }
    return true;
}

} // namespace qp
