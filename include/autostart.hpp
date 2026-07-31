#pragma once

#include <string>

namespace qp
{

// Preferred path for autostart / shortcuts:
// Velopack install root stub (…\QiuckPrompts\QiuckPrompts.exe) when present,
// otherwise the running exe (dev / portable).
std::wstring GetPreferredLaunchExePath();

// HKCU\Software\Microsoft\Windows\CurrentVersion\Run value "QiuckPrompts"
bool IsStartWithWindowsEnabled();
bool SetStartWithWindows(bool enable, std::wstring* error = nullptr);

// Set HKCU Run to match `want` (true = write launch path, false = delete value).
// Callers own higher-level policy (when to enable vs leave alone vs force off).
bool SyncStartWithWindows(bool want, std::wstring* error = nullptr);

} // namespace qp
