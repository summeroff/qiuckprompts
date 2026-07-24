#include "app.hpp"
#include "version.hpp"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>

namespace {

bool HasFlag(int argc, wchar_t** argv, const wchar_t* flag) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] && wcscmp(argv[i], flag) == 0) return true;
    }
    return false;
}

void PrintHelp() {
    // Prefer console if present; otherwise MessageBox.
    const wchar_t* help =
        L"QiuckPrompts — tray hotkey prompt templates\n"
        L"\n"
        L"Usage:\n"
        L"  qiuckprompts.exe [options]\n"
        L"\n"
        L"Options:\n"
        L"  --console              Allocate console for live logs\n"
        L"  --log-level=LEVEL      trace|debug|info|warn|error  (default: debug)\n"
        L"  --log-file=PATH        Override log path\n"
        L"  --paste-delay=MS       Delay before restoring clipboard (default: 80)\n"
        L"  --self-test            Run headless checks and exit\n"
        L"  --help                 Show this help\n"
        L"\n"
        L"POC: edit templates/hotkeys in include/config.hpp and rebuild.\n"
        L"Default hotkeys: Ctrl+Alt+1..5\n";

    if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        fwprintf(stdout, L"%s", help);
        FreeConsole();
    } else {
        MessageBoxW(nullptr, help, QP_APP_DISPLAY_W, MB_OK | MB_ICONINFORMATION);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        argc = 0;
    }

    // Structured exception-free top level; keep it simple for POC.
    int code = 0;

    if (HasFlag(argc, argv, L"--help") || HasFlag(argc, argv, L"-h") ||
        HasFlag(argc, argv, L"/?")) {
        PrintHelp();
        if (argv) LocalFree(argv);
        return 0;
    }

    if (HasFlag(argc, argv, L"--self-test")) {
        // Need a console for self-test output when launched as WIN32 subsystem.
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            AllocConsole();
        }
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        code = qp::App::RunSelfTest();
        if (argv) LocalFree(argv);
        return code;
    }

    qp::App app;
    code = app.Run(instance, argc, argv ? argv : nullptr);

    if (argv) LocalFree(argv);
    return code;
}
