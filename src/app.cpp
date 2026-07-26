#include "app.hpp"
#include "logger.hpp"
#include "input_sim.hpp"
#include "page_ready.hpp"
#include "title_sample.hpp"
#include "version.hpp"
#include "workflow.hpp"
#include "clipboard_image.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace qp
{

namespace
{
App* g_app = nullptr;
}

App::~App()
{
    tray_.Destroy();
    hotkeys_.UnregisterAll();
    if (hwnd_)
    {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    Logger::Instance().Shutdown();
    g_app = nullptr;
}

LRESULT CALLBACK App::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    App* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else
    {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self)
    {
        return self->WndProc(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT App::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case TrayIcon::WM_TRAYICON:
        tray_.OnTrayMessage(wParam, lParam);
        return 0;

    case WM_HOTKEY:
        hotkeys_.OnWmHotkey(wParam);
        return 0;

    case WM_TIMER:
        hotkeys_.OnTimer(wParam);
        return 0;

    case WM_COMMAND:
        OnMenuCommand(static_cast<UINT>(LOWORD(wParam)));
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool App::CreateMessageWindow(std::wstring* error)
{
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = instance_;
    wc.lpszClassName = QP_WND_CLASS_W;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    const ATOM atom = RegisterClassExW(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        if (error)
            *error = L"RegisterClassEx failed: " + LastErrorMessage();
        return false;
    }

    hwnd_ = CreateWindowExW(0, QP_WND_CLASS_W, QP_APP_DISPLAY_W, WS_OVERLAPPED, 0, 0, 0, 0,
                            HWND_MESSAGE, // message-only window
                            nullptr, instance_, this);

    if (!hwnd_)
    {
        if (error)
            *error = L"CreateWindowEx failed: " + LastErrorMessage();
        return false;
    }

    QP_LOG_DEBUG(L"message window created hwnd=%p", hwnd_);
    return true;
}

bool App::InitTray(std::wstring* error)
{
    std::wstring tip = std::wstring(QP_APP_DISPLAY_W) + L" v" + Utf8ToWide(QP_VERSION_STRING);
    if (!tray_.Create(instance_, hwnd_, tip, error))
    {
        return false;
    }
    tray_.SetMenuHandler([this](UINT cmd) { OnMenuCommand(cmd); });
    return true;
}

bool App::RegisterHotkeys(std::wstring* error)
{
    hotkeys_.SetTriggerMode(cfg_.hotkeyTrigger);
    hotkeys_.SetReleaseTimeoutMs(cfg_.hotkeyReleaseTimeoutMs);
    hotkeys_.SetReleasePollMs(cfg_.hotkeyReleasePollMs);
    hotkeys_.SetCallback([this](int id, const HotkeyBinding& b) { OnHotkey(id, b); });
    if (cfg_.bindings.empty())
    {
        GetBuiltinBindings(cfg_.bindings);
    }
    return hotkeys_.RegisterAll(hwnd_, cfg_.bindings, error);
}

void App::OnHotkey(int /*id*/, const HotkeyBinding& binding)
{
    // Re-entrancy guard: SendToAi can take seconds; ignore nested fires.
    if (hotkeys_.Busy())
    {
        QP_LOG_WARN(L"hotkey: nested fire ignored (%s)", binding.hotkey.display.c_str());
        return;
    }
    hotkeys_.SetBusy(true);

    QP_LOG_INFO(L"hotkey fired: %s (%s) -> template '%s' action=%s", binding.hotkey.display.c_str(),
                binding.label.c_str(), binding.templateId.c_str(), ActionKindName(binding.action));

    // Capture titles at the moment the action starts (source editor, etc.)
    LogForegroundTitle(L"hotkey_fire", binding.hotkey.display);
    LogBrowserTitleSweep(L"hotkey_fire_sweep");

    std::wstring body = binding.promptBody;
    if (body.empty())
    {
        QP_LOG_ERROR(L"empty prompt for binding '%s'", binding.name.c_str());
        hotkeys_.SetBusy(false);
        return;
    }

    const ActionKind action = cfg_.forceInsertOnly ? ActionKind::InsertTemplate : binding.action;

    std::wstring err;
    bool ok = false;
    if (action == ActionKind::SendToAi)
    {
        WorkflowRequest req;
        req.promptBody = body;
        req.aiUrl = binding.aiUrl;
        req.pageTitleHint = binding.pageTitleHint;
        req.captureEditor = binding.captureEditor;
        req.requireClipboardImage = binding.requireClipboardImage;
        req.fenceEditorText = binding.fenceEditorText;
        req.service = binding.service;
        req.label = binding.label;
        ok = workflow_.Run(req, &err);
    } else
    {
        ok = injector_.Inject(body, &err);
    }

    if (!ok)
    {
        QP_LOG_ERROR(L"action failed for '%s': %s", binding.templateId.c_str(), err.c_str());
    }

    // After workflow: what is focused / browser titles look like now?
    LogForegroundTitle(L"hotkey_done", binding.templateId);
    LogBrowserTitleSweep(L"hotkey_done_sweep");

    hotkeys_.SetBusy(false);
}

void App::OnMenuCommand(UINT cmd)
{
    switch (cmd)
    {
    case TrayIcon::IdExit:
        QP_LOG_INFO(L"exit requested from tray");
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        break;
    case TrayIcon::IdOpenLog:
        if (!logPath_.empty())
        {
            if (!OpenTextFile(logPath_))
            {
                OpenInExplorer(logPath_);
            }
        }
        break;
    case TrayIcon::IdAbout:
        ShowAbout();
        break;
    case TrayIcon::IdListHotkeys:
        ShowHotkeyList();
        break;
    case TrayIcon::IdToggleInsertOnly:
        cfg_.forceInsertOnly = !cfg_.forceInsertOnly;
        QP_LOG_INFO(L"forceInsertOnly=%d", cfg_.forceInsertOnly ? 1 : 0);
        {
            std::wstring tip =
                std::wstring(QP_APP_DISPLAY_W) + L" v" + Utf8ToWide(QP_VERSION_STRING);
            if (cfg_.forceInsertOnly)
                tip += L" [insert-only]";
            tray_.SetTooltip(tip);
        }
        break;
    case TrayIcon::IdSampleTitles:
        QP_LOG_INFO(L"manual title sample requested");
        LogBrowserTitleSweep(L"tray_sample");
        break;
    case TrayIcon::IdOpenTitlesLog: {
        const std::wstring& p = TitleSampleLogPath();
        if (!p.empty())
        {
            if (!OpenTextFile(p))
                OpenInExplorer(p);
        } else if (!logPath_.empty())
        {
            OpenInExplorer(logPath_);
        }
        break;
    }
    default:
        break;
    }
}

void App::ShowAbout()
{
    std::wstring text;
    text += QP_APP_DISPLAY_W;
    text += L" v";
    text += Utf8ToWide(QP_VERSION_STRING);
    text += L"\n\nHotkeys run a Send-to-AI workflow:\n"
            L"  select-all → copy → Chrome Beta → new tab → AI URL → paste\n\n";
    text += L"AI URL: ";
    text += cfg_.workflow.defaultAiUrl;
    text += L"\nBrowser hint: ";
    text += cfg_.workflow.browserTitleHint;
    text += L"\nMode: ";
    text += cfg_.forceInsertOnly ? L"insert-only" : L"send-to-AI";
    text += L"\n\nLog: ";
    text += logPath_.empty() ? L"(none)" : logPath_;
    text += L"\nTitles: ";
    text += TitleSampleLogPath().empty() ? L"(none)" : TitleSampleLogPath();
    text += L"\n\nTray → Sample window titles now  (after opening AI tabs)\n"
            L"Then open titles.log and look for TITLE_SAMPLE lines.";

    MessageBoxW(nullptr, text.c_str(), QP_APP_DISPLAY_W, MB_OK | MB_ICONINFORMATION);
}

void App::ShowHotkeyList()
{
    std::wstring text = L"Active hotkeys:\n\n";
    for (const auto& b : hotkeys_.Bindings())
    {
        text += b.hotkey.display;
        text += L"  —  ";
        text += b.label;
        text += L"  [";
        text += b.name.empty() ? b.templateId : b.name;
        text += L"]  ";
        if (!b.service.empty())
        {
            text += b.service;
            text += L"  ";
        }
        text += ActionKindName(b.action);
        if (b.requireClipboardImage)
            text += L"  [image]";
        text += L"\n";
    }
    if (hotkeys_.Bindings().empty())
    {
        text += L"(none registered)\n";
    }
    text += L"\nLeft hand: Ctrl+Alt   Right hand: J K L I O";
    text += L"\nTrigger: ";
    text += HotkeyTriggerModeName(cfg_.hotkeyTrigger);
    text += L" (action runs after you let go)";
    if (cfg_.forceInsertOnly)
    {
        text += L"\n(currently forced insert-only via tray toggle)";
    }
    MessageBoxW(nullptr, text.c_str(), L"Hotkeys", MB_OK | MB_ICONINFORMATION);
}

int App::Run(HINSTANCE instance, int argc, wchar_t** argv)
{
    instance_ = instance;
    g_app = this;

    std::wstring err;
    if (!ParseCommandLine(argc, argv, cfg_, &err))
    {
        MessageBoxW(nullptr, err.c_str(), QP_APP_DISPLAY_W, MB_OK | MB_ICONERROR);
        return 2;
    }

    if (cfg_.console)
    {
        if (AllocConsole())
        {
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
            SetConsoleTitleW(QP_APP_DISPLAY_W);
        }
    }

    if (!single_.Acquire(QP_MUTEX_NAME_W))
    {
        MessageBoxW(nullptr,
                    L"QiuckPrompts is already running.\n"
                    L"Check the notification area (system tray).",
                    QP_APP_DISPLAY_W, MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // Log path default: <exe>/logs/qiuckprompts.log  (portable, easy to find while developing)
    logPath_ = cfg_.logPath;
    if (logPath_.empty())
    {
        logPath_ = PathJoin({GetExeDir(), L"logs", L"qiuckprompts.log"});
    }

    Logger::Instance().Init(logPath_, cfg_.logLevel, cfg_.console);

    // Dedicated title collection file (stable TITLE_SAMPLE lines for grepping).
    {
        const std::wstring titlesPath = PathJoin({GetExeDir(), L"logs", L"titles.log"});
        SetTitleSampleLogPath(titlesPath);
    }

    QP_LOG_INFO(L"=== %s v%s starting ===", QP_APP_DISPLAY_W,
                Utf8ToWide(QP_VERSION_STRING).c_str());
    QP_LOG_INFO(L"exe=%s", GetExePath().c_str());
    QP_LOG_INFO(L"log=%s level=%s pasteDelayMs=%d insertOnly=%d", logPath_.c_str(),
                Logger::LevelName(cfg_.logLevel), cfg_.pasteDelayMs, cfg_.forceInsertOnly ? 1 : 0);
    QP_LOG_INFO(L"titles.log=%s  (grep TITLE_SAMPLE)", TitleSampleLogPath().c_str());
    QP_LOG_INFO(L"hotkey trigger=%s releaseTimeoutMs=%d pollMs=%d",
                HotkeyTriggerModeName(cfg_.hotkeyTrigger), cfg_.hotkeyReleaseTimeoutMs,
                cfg_.hotkeyReleasePollMs);
    QP_LOG_INFO(L"workflow aiUrl=%s browserHint=%s pageReadyTimeoutMs=%d uia=%d",
                cfg_.workflow.defaultAiUrl.c_str(), cfg_.workflow.browserTitleHint.c_str(),
                cfg_.workflow.pageReadyTimeoutMs, cfg_.workflow.pageReadyUseUia ? 1 : 0);

    // Load bindings from config file (hotkey + prompt + service URL).
    {
        std::wstring cerr;
        const std::wstring tryPath = cfg_.configPath; // may be set by --config
        if (!LoadConfigFile(tryPath, cfg_, &cerr))
        {
            QP_LOG_WARN(L"config file not loaded (%s) — using built-in bindings", cerr.c_str());
            GetBuiltinBindings(cfg_.bindings);
        } else
        {
            QP_LOG_INFO(L"config loaded: %s (%zu bindings)", cfg_.configPath.c_str(),
                        cfg_.bindings.size());
        }
    }

    // Baseline snapshot at startup (whatever browsers are already open).
    LogBrowserTitleSweep(L"startup");

    EnsureComInitialized();
    injector_.SetPasteDelayMs(cfg_.pasteDelayMs);
    workflow_.SetConfig(cfg_.workflow);

    if (!CreateMessageWindow(&err))
    {
        QP_LOG_ERROR(L"%s", err.c_str());
        MessageBoxW(nullptr, err.c_str(), QP_APP_DISPLAY_W, MB_OK | MB_ICONERROR);
        return 3;
    }

    if (!InitTray(&err))
    {
        QP_LOG_ERROR(L"%s", err.c_str());
        MessageBoxW(nullptr, err.c_str(), QP_APP_DISPLAY_W, MB_OK | MB_ICONERROR);
        return 4;
    }

    if (!RegisterHotkeys(&err))
    {
        QP_LOG_ERROR(L"hotkey registration failed: %s", err.c_str());
        MessageBoxW(nullptr,
                    (L"Failed to register hotkeys:\n" + err +
                     L"\n\nAnother app may own these chords. "
                     L"Edit GetBuiltInBindings() in config.hpp and rebuild.")
                        .c_str(),
                    QP_APP_DISPLAY_W, MB_OK | MB_ICONWARNING);
        // Continue running so user can still open About / Exit from tray.
    } else
    {
        QP_LOG_INFO(L"%zu hotkey(s) active", hotkeys_.RegisteredCount());
    }

    // Dump bindings at debug level for traceability
    for (const auto& b : hotkeys_.Bindings())
    {
        QP_LOG_DEBUG(L"  binding id=%d %s -> %s (%s)", b.id, b.hotkey.display.c_str(),
                     b.templateId.c_str(), ActionKindName(b.action));
    }

    QP_LOG_INFO(L"ready — message loop");

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    QP_LOG_INFO(L"=== shutting down (exit=%d) ===", static_cast<int>(msg.wParam));
    tray_.Destroy();
    hotkeys_.UnregisterAll();
    Logger::Instance().Shutdown();
    return static_cast<int>(msg.wParam);
}

int App::RunSelfTest()
{
    // Headless checks — no tray, no message loop. Returns 0 on success.
    int failures = 0;

    auto expect = [&](bool cond, const wchar_t* name) {
        if (!cond)
        {
            wprintf(L"[FAIL] %s\n", name);
            ++failures;
        } else
        {
            wprintf(L"[ OK ] %s\n", name);
        }
    };

    wprintf(L"qiuckprompts self-test\n");

    std::vector<HotkeyBinding> bindings;
    GetBuiltinBindings(bindings);
    expect(!bindings.empty(), L"builtin bindings non-empty");
    for (const auto& b : bindings)
    {
        expect(b.hotkey.vk != 0, L"binding vk non-zero");
        expect(!b.name.empty() || !b.templateId.empty(), L"binding name set");
        expect(!b.promptBody.empty(), L"binding prompt non-empty");
    }

    const std::wstring disp = FormatHotkeyDisplay(MOD_CONTROL | MOD_ALT, 'J');
    expect(disp.find(L"Ctrl") != std::wstring::npos, L"display contains Ctrl");
    expect(disp.find(L"J") != std::wstring::npos, L"display contains J");
    expect(bindings[0].action == ActionKind::SendToAi, L"default action SendToAi");
    expect(bindings[0].hotkey.vk == static_cast<UINT>('J'), L"first hotkey is J");

    expect(std::wstring(HotkeyTriggerModeName(HotkeyTriggerMode::OnRelease)) == L"OnRelease",
           L"trigger mode name");
    AppConfig ac;
    expect(ac.hotkeyTrigger == HotkeyTriggerMode::OnRelease, L"default OnRelease");
    expect(ac.hotkeyReleaseTimeoutMs > 0, L"release timeout default");

    WorkflowConfig wc;
    expect(!wc.defaultAiUrl.empty(), L"default AI URL set");
    expect(!wc.browserTitleHint.empty(), L"browser hint set");
    expect(wc.pageReadyTimeoutMs > 0, L"pageReadyTimeoutMs > 0");
    expect(wc.fenceEditorText == true, L"default fenceEditorText on");

    {
        HotkeySpec hs;
        std::wstring e;
        expect(ParseHotkey(L"Ctrl+Alt+J", hs, &e) && hs.vk == static_cast<UINT>(L'J'),
               L"ParseHotkey Ctrl+Alt+J");
    }
    {
        const std::wstring p = BuildPromptPayload(L"Fix this", L"hello", true);
        expect(p.find(L"Fix this") != std::wstring::npos && p.find(L"```") != std::wstring::npos,
               L"BuildPromptPayload fence");
        const std::wstring p2 = BuildPromptPayload(L"X {{TEXT}} Y", L"ZZ", true);
        expect(p2.find(L"ZZ") != std::wstring::npos && p2.find(L"{{TEXT}}") == std::wstring::npos,
               L"BuildPromptPayload {{TEXT}}");
    }
    expect(ClipboardHasImage() == ClipboardHasImage(), L"ClipboardHasImage callable");

    {
        const std::wstring prompt = L"Please proofread the following text";
        const std::wstring text = L"hello world";
        const std::wstring fenced = AiWorkflow::ComposePayload(prompt, text, true);
        expect(fenced.find(prompt) == 0, L"fenced payload starts with prompt");
        expect(fenced.find(L":") != std::wstring::npos, L"fenced payload has colon");
        expect(fenced.find(L"```") != std::wstring::npos, L"fenced payload has fences");
        expect(fenced.find(text) != std::wstring::npos, L"fenced payload contains text");
    }

    // Load sample config from source tree if present
    {
        AppConfig loaded;
        std::wstring err;
        const std::wstring tryBesideExe = PathJoin(GetExeDir(), L"config/qiuckprompts.ini");
        bool ok = LoadConfigFile(tryBesideExe, loaded, &err);
        if (!ok)
        {
            ok =
                LoadConfigFile(L"C:/work/repos/qiuckprompts/config/qiuckprompts.ini", loaded, &err);
        }
        if (ok)
        {
            expect(!loaded.bindings.empty(), L"LoadConfigFile bindings");
            bool hasShot = false;
            for (const auto& b : loaded.bindings)
            {
                if (b.requireClipboardImage)
                    hasShot = true;
            }
            expect(hasShot, L"config has screenshot binding");
            wprintf(L"[ OK ] LoadConfigFile (%zu bindings)\n", loaded.bindings.size());
        } else
        {
            wprintf(L"[SKIP] LoadConfigFile (%s)\n", err.c_str());
        }
    }

    expect(TitleHintFromUrl(L"https://www.meta.ai/") == L"Meta", L"hint meta.ai");
    expect(TitleHintFromUrl(L"https://gemini.google.com/app") == L"Gemini", L"hint gemini");
    expect(TitleHintFromUrl(L"https://grok.com/") == L"Grok", L"hint grok");
    expect(EnsureComInitialized(), L"EnsureComInitialized");

    {
        const std::wstring sample = L"qiuckprompts-clip-helper";
        std::wstring err;
        expect(ClipboardWriteUnicode(sample, &err), L"ClipboardWriteUnicode");
        std::wstring got;
        expect(ClipboardReadUnicode(got, &err) && got == sample,
               L"ClipboardReadUnicode round-trip");
    }

    expect(!GetExeDir().empty(), L"GetExeDir");
    expect(!GetExePath().empty(), L"GetExePath");
    const std::wstring logDir = PathJoin(GetExeDir(), L"logs");
    expect(EnsureDirectory(logDir), L"EnsureDirectory(logs)");
    const std::wstring logFile = PathJoin(logDir, L"self-test.log");
    Logger::Instance().Init(logFile, LogLevel::Trace, true);
    QP_LOG_INFO(L"self-test log line");
    Logger::Instance().Shutdown();
    expect(FileExists(logFile), L"log file created");

    wprintf(L"\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace qp
