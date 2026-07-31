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

// Apply cfg preference to the Run key (call once after config load).
// Does not clear an existing Run entry when prefer=false if user enabled via tray —
// only writes when prefer=true, or when force=true.
// Simpler: SyncStartWithWindows(bool want) always sets registry to match want.
bool SyncStartWithWindows(bool want, std::wstring* error = nullptr);

} // namespace qp
