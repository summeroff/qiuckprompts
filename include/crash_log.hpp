#pragma once

#include <windows.h>

#include <string>

namespace qp
{

// Best-effort crash breadcrumbs into the same log file as Logger (no Sentry yet).
// Safe-ish from an unhandled exception filter: appends with its own handle, no mutex.

// Call once after the log path is known (may be called again if path changes).
void SetCrashLogPath(const std::wstring& logFilePath);

// Install process-wide handlers: unhandled SEH, purecall, invalid-parameter,
// and std::terminate. (abort() itself is not hooked; purecall/invalid-param call abort after logging.)
// Idempotent. Call early in wWinMain before the main app runs.
void InstallCrashHandlers();

} // namespace qp
