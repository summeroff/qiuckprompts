#pragma once

#include <windows.h>

#include "config.hpp"
#include "hotkeys.hpp"
#include "injector.hpp"
#include "tray.hpp"
#include "util.hpp"
#include "workflow.hpp"

#include <atomic>
#include <mutex>
#include <string>

namespace qp
{

class App
{
public:
    App() = default;
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    int Run(HINSTANCE instance, int argc, wchar_t** argv);
    static int RunSelfTest();

    // Posted by the workflow worker when a hotkey action finishes.
    static constexpr UINT WM_QP_WORK_DONE = WM_APP + 2;

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

    bool StartWorkThread(std::wstring* error);
    void StopWorkThread();
    bool QueueHotkeyWork(const HotkeyBinding& binding);
    void RunHotkeyWork(const HotkeyBinding& binding);
    void OnWorkDone();
    static DWORD WINAPI WorkThreadMain(void* self);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    AppConfig cfg_;
    HotkeyManager hotkeys_;
    TextInjector injector_;
    AiWorkflow workflow_{WorkflowConfig{}};
    TrayIcon tray_;
    SingleInstance single_;
    std::wstring logPath_;

    HANDLE workStop_ = nullptr;
    HANDLE workWake_ = nullptr;
    HANDLE workThread_ = nullptr;
    std::mutex workMutex_;
    HotkeyBinding workBinding_;
    bool workHasItem_ = false;
    bool workOk_ = false;
    std::wstring workErr_;
    std::wstring workTemplateId_;
    std::atomic<bool> workStopping_{false};
};

} // namespace qp
