#pragma once

#include <windows.h>
#include <shellapi.h>

#include <functional>
#include <string>

namespace qp
{

class TrayIcon
{
public:
    static constexpr UINT WM_TRAYICON = WM_APP + 1;

    enum MenuId : UINT
    {
        IdExit = 1001,
        IdOpenLog = 1002,
        IdAbout = 1003,
        IdListHotkeys = 1004,
        IdToggleInsertOnly = 1005,
        IdSampleTitles = 1006,
        IdOpenTitlesLog = 1007,
        IdOpenDataDir = 1008,
        IdOpenConfig = 1009,
        IdCheckUpdates = 1010,
    };

    TrayIcon() = default;
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Create(HINSTANCE instance, HWND hwnd, const std::wstring& tip,
                std::wstring* error = nullptr);
    void Destroy();
    void SetTooltip(const std::wstring& tip);
    void OnTrayMessage(WPARAM wParam, LPARAM lParam);

    using MenuHandler = std::function<void(UINT cmd)>;
    void SetMenuHandler(MenuHandler h) { menuHandler_ = std::move(h); }

private:
    void ShowContextMenu();

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    bool added_ = false;
    MenuHandler menuHandler_;
    NOTIFYICONDATAW nid_{};
};

} // namespace qp
