#pragma once

#include <windows.h>

#include <string>

namespace qp
{

// Lightweight Velopack-oriented updater: no Rust/C++ Velopack DLL.
// When the app is installed by Velopack Setup/Portable, Update.exe sits next to
// the install root. We fetch releases.win.json over HTTPS, download the full
// nupkg, then run: Update.exe apply --package <nupkg> --waitPid <pid>
//
// Dev builds (exe not under a Velopack layout) report "not installed" and skip.

struct UpdateCheckResult
{
    bool ok = false;        // transport/parse succeeded
    bool installed = false; // Velopack layout detected
    bool updateAvailable = false;
    std::wstring currentVersion; // major.minor.patch from this binary
    std::wstring remoteVersion;
    std::wstring packageFileName; // e.g. QiuckPrompts-0.3.2-full.nupkg
    std::wstring packageUrl;      // absolute HTTPS URL
    std::wstring detail;          // human message / error
};

// Default feed base (directory containing releases.win.json + nupkg assets).
// Override with ini update_url= or --update-url=
std::wstring DefaultUpdateFeedUrl();

// True if Update.exe is found for this process (Velopack install/portable).
bool IsVelopackInstalled();

// Path to Update.exe if installed, else empty.
std::wstring FindUpdateExe();

// GET releases.win.json from feedUrl and compare to this build's version.
bool CheckForUpdates(const std::wstring& feedUrl, UpdateCheckResult& out,
                     std::wstring* error = nullptr);

// Download package then invoke Update.exe apply (restarts app). Returns false if
// prepare failed; on success this process is expected to exit soon.
bool DownloadAndApplyUpdate(const UpdateCheckResult& check, std::wstring* error = nullptr);

// One-shot tray helper: check → prompt → download/apply. Runs on caller thread
// (blocks UI briefly for check; download can take longer — keep UX simple).
void RunUpdateFlowInteractive(const std::wstring& feedUrl, HWND owner);

// Velopack Setup/Update invokes the main exe with fast-exit hooks:
//   --veloapp-install VERSION
//   --veloapp-updated VERSION
//   --veloapp-obsolete VERSION
//   --veloapp-uninstall VERSION
// Handle them *before* normal CLI parsing (unknown flags would MessageBox-fail).
// Returns true if a hook was handled; caller must exit with *exitCode (0 on success).
bool TryHandleVelopackHook(int argc, wchar_t** argv, int* exitCode);

// Stable unpacked-extension directory outside current\ (survives Velopack updates):
// %LOCALAPPDATA%\QiuckPrompts\extension
std::wstring GetStableExtensionDir(bool ensure = true);

// Copy packaged extension/ (next to exe) → stable AppData extension dir.
bool SyncPackagedExtensionToStable(std::wstring* error = nullptr);

} // namespace qp
