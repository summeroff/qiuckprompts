#pragma once

#include <windows.h>

#include "config.hpp"
#include "hotkeys.hpp"
#include "injector.hpp"
#include "tray.hpp"
#include "util.hpp"

namespace qp {

class App {
public:
    App() = default;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run(HINSTANCE instance, int argc, wchar_t** argv);
    static int RunSelfTest();

private:
    static LRESULT CALLBACK StaticWndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool CreateMessageWindow(std::wstring* error);
    bool InitTray(std::wstring* error);
    bool RegisterHotkeys(std::wstring* error);

    void OnHotkey(int id, const HotkeyBinding& binding);
    void OnMenuCommand(UINT cmd);
    void ShowAbout();
    void ShowHotkeyList();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    AppConfig cfg_;
    HotkeyManager hotkeys_;
    TextInjector injector_;
    TrayIcon tray_;
    SingleInstance single_;
    std::wstring logPath_;
};

} // namespace qp
