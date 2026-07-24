#include "app.hpp"
#include "logger.hpp"
#include "version.hpp"

#include <cstdio>
#include <string>

namespace qp {

namespace {
App* g_app = nullptr;
}

App::~App() {
    tray_.Destroy();
    hotkeys_.UnregisterAll();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    Logger::Instance().Shutdown();
    g_app = nullptr;
}

LRESULT CALLBACK App::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) {
        return self->WndProc(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT App::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case TrayIcon::WM_TRAYICON:
        tray_.OnTrayMessage(wParam, lParam);
        return 0;

    case WM_HOTKEY:
        hotkeys_.OnWmHotkey(wParam);
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

bool App::CreateMessageWindow(std::wstring* error) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = instance_;
    wc.lpszClassName = QP_WND_CLASS_W;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    const ATOM atom = RegisterClassExW(&wc);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        if (error) *error = L"RegisterClassEx failed: " + LastErrorMessage();
        return false;
    }

    hwnd_ = CreateWindowExW(
        0,
        QP_WND_CLASS_W,
        QP_APP_DISPLAY_W,
        WS_OVERLAPPED,
        0, 0, 0, 0,
        HWND_MESSAGE, // message-only window
        nullptr,
        instance_,
        this);

    if (!hwnd_) {
        if (error) *error = L"CreateWindowEx failed: " + LastErrorMessage();
        return false;
    }

    QP_LOG_DEBUG(L"message window created hwnd=%p", hwnd_);
    return true;
}

bool App::InitTray(std::wstring* error) {
    std::wstring tip = std::wstring(QP_APP_DISPLAY_W) + L" v" +
                       Utf8ToWide(QP_VERSION_STRING);
    if (!tray_.Create(instance_, hwnd_, tip, error)) {
        return false;
    }
    tray_.SetMenuHandler([this](UINT cmd) { OnMenuCommand(cmd); });
    return true;
}

bool App::RegisterHotkeys(std::wstring* error) {
    hotkeys_.SetCallback([this](int id, const HotkeyBinding& b) { OnHotkey(id, b); });
    auto bindings = GetBuiltInBindings();
    return hotkeys_.RegisterAll(hwnd_, std::move(bindings), error);
}

void App::OnHotkey(int /*id*/, const HotkeyBinding& binding) {
    QP_LOG_INFO(L"hotkey fired: %s (%s) -> template '%s'",
                binding.hotkey.display.c_str(),
                binding.label.c_str(),
                binding.templateId.c_str());

    std::wstring body;
    if (!FindTemplateBody(binding.templateId, body)) {
        QP_LOG_ERROR(L"template not found: %s", binding.templateId.c_str());
        return;
    }

    std::wstring err;
    if (!injector_.Inject(body, &err)) {
        QP_LOG_ERROR(L"injection failed for '%s': %s",
                     binding.templateId.c_str(), err.c_str());
    }
}

void App::OnMenuCommand(UINT cmd) {
    switch (cmd) {
    case TrayIcon::IdExit:
        QP_LOG_INFO(L"exit requested from tray");
        PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        break;
    case TrayIcon::IdOpenLog:
        if (!logPath_.empty()) {
            if (!OpenTextFile(logPath_)) {
                // Fall back to folder
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
    default:
        break;
    }
}

void App::ShowAbout() {
    std::wstring text;
    text += QP_APP_DISPLAY_W;
    text += L" v";
    text += Utf8ToWide(QP_VERSION_STRING);
    text += L"\n\nTray tool: global hotkeys insert prompt templates\n";
    text += L"into the current text field (clipboard + Ctrl+V).\n\n";
    text += L"Log: ";
    text += logPath_.empty() ? L"(none)" : logPath_;
    text += L"\n\nPOC: templates/hotkeys are compiled-in (see config.hpp).";

    MessageBoxW(nullptr, text.c_str(), QP_APP_DISPLAY_W, MB_OK | MB_ICONINFORMATION);
}

void App::ShowHotkeyList() {
    std::wstring text = L"Active hotkeys:\n\n";
    for (const auto& b : hotkeys_.Bindings()) {
        text += b.hotkey.display;
        text += L"  —  ";
        text += b.label;
        text += L"  [";
        text += b.templateId;
        text += L"]\n";
    }
    if (hotkeys_.Bindings().empty()) {
        text += L"(none registered)\n";
    }
    MessageBoxW(nullptr, text.c_str(), L"Hotkeys", MB_OK | MB_ICONINFORMATION);
}

int App::Run(HINSTANCE instance, int argc, wchar_t** argv) {
    instance_ = instance;
    g_app = this;

    std::wstring err;
    if (!ParseCommandLine(argc, argv, cfg_, &err)) {
        MessageBoxW(nullptr, err.c_str(), QP_APP_DISPLAY_W, MB_OK | MB_ICONERROR);
        return 2;
    }

    if (cfg_.console) {
        if (AllocConsole()) {
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
            SetConsoleTitleW(QP_APP_DISPLAY_W);
        }
    }

    if (!single_.Acquire(QP_MUTEX_NAME_W)) {
        MessageBoxW(nullptr,
                    L"QiuckPrompts is already running.\n"
                    L"Check the notification area (system tray).",
                    QP_APP_DISPLAY_W, MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // Log path default: <exe>/logs/qiuckprompts.log  (portable, easy to find while developing)
    logPath_ = cfg_.logPath;
    if (logPath_.empty()) {
        logPath_ = PathJoin({GetExeDir(), L"logs", L"qiuckprompts.log"});
    }

    Logger::Instance().Init(logPath_, cfg_.logLevel, cfg_.console);
    QP_LOG_INFO(L"=== %s v%s starting ===", QP_APP_DISPLAY_W, Utf8ToWide(QP_VERSION_STRING).c_str());
    QP_LOG_INFO(L"exe=%s", GetExePath().c_str());
    QP_LOG_INFO(L"log=%s level=%s pasteDelayMs=%d",
                logPath_.c_str(),
                Logger::LevelName(cfg_.logLevel),
                cfg_.pasteDelayMs);

    injector_.SetPasteDelayMs(cfg_.pasteDelayMs);

    if (!CreateMessageWindow(&err)) {
        QP_LOG_ERROR(L"%s", err.c_str());
        MessageBoxW(nullptr, err.c_str(), QP_APP_DISPLAY_W, MB_OK | MB_ICONERROR);
        return 3;
    }

    if (!InitTray(&err)) {
        QP_LOG_ERROR(L"%s", err.c_str());
        MessageBoxW(nullptr, err.c_str(), QP_APP_DISPLAY_W, MB_OK | MB_ICONERROR);
        return 4;
    }

    if (!RegisterHotkeys(&err)) {
        QP_LOG_ERROR(L"hotkey registration failed: %s", err.c_str());
        MessageBoxW(nullptr,
                    (L"Failed to register hotkeys:\n" + err +
                     L"\n\nAnother app may own these chords. "
                     L"Edit GetBuiltInBindings() in config.hpp and rebuild.")
                        .c_str(),
                    QP_APP_DISPLAY_W, MB_OK | MB_ICONWARNING);
        // Continue running so user can still open About / Exit from tray.
    } else {
        QP_LOG_INFO(L"%zu hotkey(s) active", hotkeys_.RegisteredCount());
    }

    // Dump bindings at debug level for traceability
    for (const auto& b : hotkeys_.Bindings()) {
        QP_LOG_DEBUG(L"  binding id=%d %s -> %s",
                     b.id, b.hotkey.display.c_str(), b.templateId.c_str());
    }

    QP_LOG_INFO(L"ready — message loop");

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    QP_LOG_INFO(L"=== shutting down (exit=%d) ===", static_cast<int>(msg.wParam));
    tray_.Destroy();
    hotkeys_.UnregisterAll();
    Logger::Instance().Shutdown();
    return static_cast<int>(msg.wParam);
}

int App::RunSelfTest() {
    // Headless checks — no tray, no message loop. Returns 0 on success.
    int failures = 0;

    auto expect = [&](bool cond, const wchar_t* name) {
        if (!cond) {
            wprintf(L"[FAIL] %s\n", name);
            ++failures;
        } else {
            wprintf(L"[ OK ] %s\n", name);
        }
    };

    wprintf(L"qiuckprompts self-test\n");

    // Templates resolve
    const BuiltInTemplate* t = nullptr;
    size_t n = 0;
    GetBuiltInTemplates(t, n);
    expect(n >= 3, L"builtin templates count >= 3");
    expect(t && t[0].id && t[0].body && wcslen(t[0].body) > 0, L"first template non-empty");

    std::wstring body;
    expect(FindTemplateBody(L"grammar_check", body) && !body.empty(),
           L"FindTemplateBody(grammar_check)");
    expect(!FindTemplateBody(L"no_such_template_xyz", body),
           L"missing template returns false");

    // Bindings well-formed
    auto bindings = GetBuiltInBindings();
    expect(!bindings.empty(), L"builtin bindings non-empty");
    for (const auto& b : bindings) {
        expect(b.hotkey.vk != 0, L"binding vk non-zero");
        expect(!b.templateId.empty(), L"binding templateId set");
        std::wstring tmp;
        expect(FindTemplateBody(b.templateId, tmp), L"binding template exists");
    }

    // Hotkey display
    const std::wstring disp = FormatHotkeyDisplay(MOD_CONTROL | MOD_ALT, '1');
    expect(disp.find(L"Ctrl") != std::wstring::npos, L"display contains Ctrl");
    expect(disp.find(L"1") != std::wstring::npos, L"display contains 1");

    // Paths
    expect(!GetExeDir().empty(), L"GetExeDir");
    expect(!GetExePath().empty(), L"GetExePath");

    // Path join / ensure log dir
    const std::wstring logDir = PathJoin(GetExeDir(), L"logs");
    expect(EnsureDirectory(logDir), L"EnsureDirectory(logs)");

    // Logger round-trip
    const std::wstring logFile = PathJoin(logDir, L"self-test.log");
    Logger::Instance().Init(logFile, LogLevel::Trace, true);
    QP_LOG_INFO(L"self-test log line");
    Logger::Instance().Shutdown();
    expect(FileExists(logFile), L"log file created");

    // Clipboard set/get (no paste)
    {
        TextInjector inj(0);
        const std::wstring sample = L"qiuckprompts-self-test-unicode-✓";
        // Use internal path via public Inject would paste — instead probe util via OpenClipboard
        // Minimal: write clipboard ourselves
        if (OpenClipboard(nullptr)) {
            EmptyClipboard();
            const size_t bytes = (sample.size() + 1) * sizeof(wchar_t);
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            expect(mem != nullptr, L"GlobalAlloc clipboard");
            if (mem) {
                void* p = GlobalLock(mem);
                memcpy(p, sample.c_str(), bytes);
                GlobalUnlock(mem);
                expect(SetClipboardData(CF_UNICODETEXT, mem) != nullptr, L"SetClipboardData");
            }
            CloseClipboard();

            if (OpenClipboard(nullptr)) {
                HANDLE h = GetClipboardData(CF_UNICODETEXT);
                expect(h != nullptr, L"GetClipboardData");
                if (h) {
                    const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h));
                    expect(p && sample == p, L"clipboard round-trip text");
                    if (p) GlobalUnlock(h);
                }
                CloseClipboard();
            }
        } else {
            wprintf(L"[SKIP] clipboard tests (OpenClipboard busy)\n");
        }
    }

    wprintf(L"\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}

} // namespace qp
