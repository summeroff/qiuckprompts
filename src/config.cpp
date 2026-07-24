#include "config.hpp"
#include "logger.hpp"

#include <cstdlib>

namespace qp {

bool ParseCommandLine(int argc, wchar_t** argv, AppConfig& cfg, std::wstring* error) {
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i] ? argv[i] : L"";
        if (arg.empty()) continue;

        if (arg == L"--console") {
            cfg.console = true;
            continue;
        }
        if (arg == L"--self-test") {
            // handled in main
            continue;
        }

        auto takeValue = [&](const wchar_t* prefix) -> std::wstring {
            const size_t n = wcslen(prefix);
            if (arg.rfind(prefix, 0) == 0 && arg.size() > n) {
                return arg.substr(n);
            }
            // --flag VALUE form
            if (arg == std::wstring(prefix, prefix + n - (prefix[n - 1] == L'=' ? 1 : 0))) {
                if (i + 1 < argc && argv[i + 1]) {
                    return argv[++i];
                }
            }
            return {};
        };

        if (arg.rfind(L"--log-level=", 0) == 0 || arg == L"--log-level") {
            std::wstring v = (arg == L"--log-level")
                                 ? (i + 1 < argc ? argv[++i] : L"")
                                 : arg.substr(wcslen(L"--log-level="));
            if (v.empty()) {
                if (error) *error = L"--log-level requires a value";
                return false;
            }
            cfg.logLevel = Logger::ParseLevel(v);
            continue;
        }

        if (arg.rfind(L"--log-file=", 0) == 0 || arg == L"--log-file") {
            std::wstring v = (arg == L"--log-file")
                                 ? (i + 1 < argc ? argv[++i] : L"")
                                 : arg.substr(wcslen(L"--log-file="));
            if (v.empty()) {
                if (error) *error = L"--log-file requires a path";
                return false;
            }
            cfg.logPath = v;
            continue;
        }

        if (arg.rfind(L"--paste-delay=", 0) == 0 || arg == L"--paste-delay") {
            std::wstring v = (arg == L"--paste-delay")
                                 ? (i + 1 < argc ? argv[++i] : L"")
                                 : arg.substr(wcslen(L"--paste-delay="));
            if (v.empty()) {
                if (error) *error = L"--paste-delay requires milliseconds";
                return false;
            }
            cfg.pasteDelayMs = _wtoi(v.c_str());
            if (cfg.pasteDelayMs < 0) cfg.pasteDelayMs = 0;
            if (cfg.pasteDelayMs > 2000) cfg.pasteDelayMs = 2000;
            continue;
        }

        if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            // main prints help
            continue;
        }

        // Unknown — ignore for POC (logged after logger init)
        (void)takeValue;
    }
    return true;
}

} // namespace qp
