#include "tray.hpp"
#include "logger.hpp"
#include "util.hpp"
#include "version.hpp"

#include "resource.h"

namespace qp {

TrayIcon::~TrayIcon() {
    Destroy();
}

bool TrayIcon::Create(HINSTANCE instance, HWND hwnd, const std::wstring& tip,
                      std::wstring* error) {
    Destroy();
    instance_ = instance;
    hwnd_ = hwnd;

    ZeroMemory(&nid_, sizeof(nid_));
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid_.uCallbackMessage = WM_TRAYICON;

    // Prefer custom icon from resources; fall back to app default.
    nid_.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APPICON));
    if (!nid_.hIcon) {
        nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        QP_LOG_WARN(L"tray: custom icon missing, using IDI_APPLICATION");
    }

    wcsncpy_s(nid_.szTip, tip.c_str(), _TRUNCATE);

    if (!Shell_NotifyIconW(NIM_ADD, &nid_)) {
        if (error) *error = L"Shell_NotifyIcon(NIM_ADD) failed: " + LastErrorMessage();
        QP_LOG_ERROR(L"%s", error ? error->c_str() : L"tray add failed");
        return false;
    }

    // Use modern icon behavior on Win10+
    nid_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid_);

    added_ = true;
    QP_LOG_INFO(L"tray icon added");
    return true;
}

void TrayIcon::Destroy() {
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        added_ = false;
        QP_LOG_DEBUG(L"tray icon removed");
    }
    if (nid_.hIcon) {
        // Only destroy if we loaded from module resources (not shared stock icon).
        // LoadIcon from resource returns shared icon — do not DestroyIcon.
        nid_.hIcon = nullptr;
    }
    hwnd_ = nullptr;
}

void TrayIcon::SetTooltip(const std::wstring& tip) {
    if (!added_) return;
    nid_.uFlags = NIF_TIP | NIF_SHOWTIP;
    wcsncpy_s(nid_.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void TrayIcon::OnTrayMessage(WPARAM /*wParam*/, LPARAM lParam) {
    const UINT mouseMsg = static_cast<UINT>(LOWORD(lParam));
    switch (mouseMsg) {
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        ShowContextMenu();
        break;
    case WM_LBUTTONDBLCLK:
        if (menuHandler_) menuHandler_(IdAbout);
        break;
    default:
        break;
    }
}

void TrayIcon::ShowContextMenu() {
    if (!hwnd_) return;

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, IdListHotkeys, L"List hotkeys");
    AppendMenuW(menu, MF_STRING, IdOpenLog, L"Open log file");
    AppendMenuW(menu, MF_STRING, IdAbout, L"About");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IdExit, L"Exit");

    POINT pt{};
    GetCursorPos(&pt);
    // Required so the menu dismisses correctly when clicking elsewhere.
    SetForegroundWindow(hwnd_);
    const UINT cmd = TrackPopupMenu(menu,
                                    TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                    pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);

    // Per tray docs: post a dummy message so the menu closes properly.
    PostMessageW(hwnd_, WM_NULL, 0, 0);

    if (cmd && menuHandler_) {
        QP_LOG_DEBUG(L"tray menu cmd=%u", cmd);
        menuHandler_(cmd);
    }
}

} // namespace qp
