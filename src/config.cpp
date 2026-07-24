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
            cfg.pasteDelayMs = _wtoi(v.c_str());
            if (cfg.pasteDelayMs < 0) cfg.pasteDelayMs = 0;
            if (cfg.pasteDelayMs > 5000) cfg.pasteDelayMs = 5000;
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
        if (TakeEqValue(i, argc, argv, arg, L"--navigate-delay", v)) {
            cfg.workflow.afterNavigateMs = _wtoi(v.c_str());
            if (cfg.workflow.afterNavigateMs < 0) cfg.workflow.afterNavigateMs = 0;
            if (cfg.workflow.afterNavigateMs > 60000) cfg.workflow.afterNavigateMs = 60000;
            continue;
        }
    }
    return true;
}

} // namespace qp
