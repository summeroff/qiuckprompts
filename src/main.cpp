#include "app.hpp"
#include "crash_log.hpp"
#include "crash_test.hpp"
#include "version.hpp"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>

namespace
{

bool HasFlag(int argc, wchar_t** argv, const wchar_t* flag)
{
    for (int i = 1; i < argc; ++i)
    {
        if (argv[i] && wcscmp(argv[i], flag) == 0)
            return true;
    }
    return false;
}

void PrintHelp()
{
    const wchar_t* help =
        L"QiuckPrompts — tray hotkeys → AI chat workflow\n"
        L"\n"
        L"Usage:\n"
        L"  qiuckprompts.exe [options]\n"
        L"\n"
        L"Default hotkeys (left Ctrl+Alt, right-hand letter):\n"
        L"  Ctrl+Alt+J  Grammar check\n"
        L"  Ctrl+Alt+K  Fact check\n"
        L"  Ctrl+Alt+L  Summarize\n"
        L"  Ctrl+Alt+I  Explain simply\n"
        L"  Ctrl+Alt+O  Code review\n"
        L"\n"
        L"Workflow: select-all → copy editor → activate Chrome Dev →\n"
        L"          new tab → open AI URL → paste prompt + text\n"
        L"\n"
        L"Options:\n"
        L"  --console                 Live logs on a console\n"
        L"  --log-level=LEVEL         trace|debug|info|warn|error\n"
        L"  --log-file=PATH           Override log path\n"
        L"  --paste-delay=MS          Insert-only clipboard restore delay\n"
        L"  --ai-url=URL              Default AI chat URL (meta.ai)\n"
        L"  --browser-hint=TEXT       Window/path hint (default: Chrome Dev)\n"
        L"  --page-title-hint=TEXT    Page title must contain this (auto from URL)\n"
        L"  --page-ready-timeout=MS   Max wait for page/input (default 15000)\n"
        L"  --page-ready-min=MS       Min wait after navigate (default 500)\n"
        L"  --no-uia                  Disable UI Automation; title-only wait\n"
        L"  --insert-only             Paste template only (no browser flow)\n"
        L"  --replace-running         If already running, close it and start this build\n"
        L"  --hotkey-on-press         Fire on key-down (default: on key-up/release)\n"
        L"  --hotkey-on-release       Fire after chord released (default)\n"
        L"  --hotkey-release-timeout=MS  Max wait for release (default 3000)\n"
        L"  --self-test               Headless checks\n"
#if QP_DEV_TOOLS
        L"  --crash-test              Dev: run app, crash on worker thread after ~2s\n"
#endif
        L"  --help                    This help\n"
        L"\n"
        L"Hotkeys arm on press and run on release so Ctrl/Alt are up before\n"
        L"select-all/copy/paste. While a workflow runs, further hotkeys are ignored.\n"
        L"\n"
        L"Page ready: Chrome inputs are NOT Win32 HWNDs. We wait on tab title\n"
        L"+ UI Automation Edit control (accessibility tree), then paste.\n"
        L"\n"
        L"Edit templates/hotkeys in include/config.hpp and rebuild.\n";

    if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole())
    {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        fwprintf(stdout, L"%s", help);
        FreeConsole();
    } else
    {
        MessageBoxW(nullptr, help, QP_APP_DISPLAY_W, MB_OK | MB_ICONINFORMATION);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    // Before anything else: SEH / purecall / invalid-parameter / terminate → stack into log.
    qp::InstallCrashHandlers();

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
    {
        argc = 0;
    }

    int code = 0;

    if (HasFlag(argc, argv, L"--help") || HasFlag(argc, argv, L"-h") || HasFlag(argc, argv, L"/?"))
    {
        PrintHelp();
        if (argv)
            LocalFree(argv);
        return 0;
    }

    if (HasFlag(argc, argv, L"--self-test"))
    {
        if (!AttachConsole(ATTACH_PARENT_PROCESS))
        {
            AllocConsole();
        }
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        code = qp::App::RunSelfTest();
        if (argv)
            LocalFree(argv);
        return code;
    }

#if !QP_DEV_TOOLS
    // Production builds: reject the flag explicitly so scripts fail closed.
    if (HasFlag(argc, argv, L"--crash-test"))
    {
        if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole())
        {
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stderr);
            fwprintf(stderr, L"qiuckprompts: --crash-test not available in this build\n");
        }
        if (argv)
            LocalFree(argv);
        return 2;
    }
#endif

    qp::App app;
    code = app.Run(instance, argc, argv ? argv : nullptr);

    if (argv)
        LocalFree(argv);
    return code;
}
