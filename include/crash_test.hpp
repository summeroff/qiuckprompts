#pragma once

// Dev-only CLI / diagnostics (e.g. --crash-test).
// Defined only for Debug (and configs where CMake enables QP_ENABLE_DEV_TOOLS).
// Production RelWithDebInfo/Release tag builds leave this undefined → code stripped.

#if defined(QP_ENABLE_DEV_TOOLS) && QP_ENABLE_DEV_TOOLS
#define QP_DEV_TOOLS 1
#else
#define QP_DEV_TOOLS 0
#endif

namespace qp
{

#if QP_DEV_TOOLS

// Run the normal app; after delayMs a worker thread crashes through a deep call
// chain so the log stack is multi-frame (not a one-liner in wWinMain).
// delayMs default ~2s so tray/hotkeys are up first.
void StartDeferredCrashTest(unsigned delayMs = 2000);

#else

inline void StartDeferredCrashTest(unsigned = 0)
{
}

#endif

} // namespace qp
